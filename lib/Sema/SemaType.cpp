#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Arith.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SemaUtil.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <algorithm>
#include <format>
#include <ranges>

using namespace plang;

// See NumTypeKinds in AstBase.h.
static_assert(NumTypeKinds == 14,
              "a new type denoter needs a case in resolveTypeImpl");

// ---------------------------------------------------------------------------
// Compile-time bound evaluation
// ---------------------------------------------------------------------------

// constBound is now a Sema member function (declared in Sema.h).
// The implementation is below, after the type-resolution section.

// ---------------------------------------------------------------------------
// Type resolution
// ---------------------------------------------------------------------------

namespace {
/// "(a, b, c)", truncated so a wide record does not fill the diagnostic.
std::string describeValueList(const std::vector<std::string>& Names) {
    constexpr size_t MaxShown = 3;
    std::string Out = "(";
    for (size_t I = 0; I < Names.size() && I < MaxShown; ++I) {
        if (I) Out += ", ";
        Out += Names[I];
    }
    if (Names.size() > MaxShown) Out += ", ...";
    return Out + ")";
}
/// An ordinal value written the way the source wrote it, so a rejected
/// `array['z'..'a']` is not reported as 122 and 97.
std::string describeBound(const Type& T, int64_t V) {
    const Type* Base = &T;
    while (Base->Kind == TypeKind::Subrange && Base->SubBase)
        Base = Base->SubBase.get();

    if (Base->Kind == TypeKind::Char && V >= 0 && V < 256)
        return "'" + std::string(1, static_cast<char>(V)) + "'";
    if (Base->Kind == TypeKind::Boolean) return V ? "true" : "false";
    if (Base->Kind == TypeKind::Enum && V >= 0
            && static_cast<size_t>(V) < Base->EnumValues.size())
        return Base->EnumValues[static_cast<size_t>(V)];
    return std::to_string(V);
}
} // namespace

/// Both bounds of an index type or subrange, or nothing once one has been
/// reported as unusable.  Both are folded before giving up so that
/// `array[lo..hi]` over two variables names them both rather than one per
/// rebuild.
///
/// ISO §6.4.2.4 requires the lower bound not to exceed the upper.  An inverted
/// pair used to be accepted and lowered to a zero-element array, which every
/// subscript then wrote past — the same silent corruption as a bound that did
/// not fold, reached by a different route.
std::optional<std::pair<int64_t, int64_t>>
Sema::foldBounds(const ExprNode& Low, const ExprNode& High,
                 const Type& Base, DiagID LowID, DiagID HighID) {
    const auto Lo = constBound(Low);
    const auto Hi = constBound(High);
    if (!Lo) error(Low.Loc,  LowID);
    if (!Hi) error(High.Loc, HighID);
    if (!Lo || !Hi) return std::nullopt;
    if (*Lo > *Hi) {
        error(Low.Loc, diag::err_bound_range_inverted,
              {describeBound(Base, *Lo), describeBound(Base, *Hi)});
        return std::nullopt;
    }
    return std::pair{*Lo, *Hi};
}

std::shared_ptr<Type> Sema::resolveType(const TypeNode& Node) {
    auto T = resolveTypeImpl(Node);
    // Record the result on the node so codegen can lower type denoters whose
    // meaning is not recoverable from the syntax alone.
    Node.ResolvedType = T;
    // A record written in a schema body is resolved once per instantiation and
    // is a different type each time, while the declaration it came from stays
    // the one node.  The discriminants it was resolved under go with it so that
    // a layout can be worked out for this instance rather than for whichever
    // one was resolved last.  Only a record built from this very node is
    // stamped: a named type resolves to a shared type object that belongs to
    // the declaration, not to the use.
    if (T && !ActiveSchemaBindings_.empty() && T->Kind == TypeKind::Record
            && T->RecordDecl == llvm::dyn_cast<RecordTypeNode>(&Node)
            && T->RecordDecl != nullptr) {
        T->SchemaBindings.assign(ActiveSchemaBindings_.begin(),
                                 ActiveSchemaBindings_.end());
        std::sort(T->SchemaBindings.begin(), T->SchemaBindings.end());
    }
    if (Node.InitialState) checkInitialState(Node, *T);
    return T;
}

// EP §6.6: the value a denoter's 'value' clause gives has to be a value of
// the type the denoter denotes, and has to be one the compiler can work out,
// since it is the state every variable of the type begins in.
void Sema::checkInitialState(const TypeNode& Node, const Type& T) {
    ExpectedValueType_ = Node.ResolvedType;
    auto VT = checkExpr(*Node.InitialState);
    ExpectedValueType_ = nullptr;
    if (!T.isError() && !VT->isError() && !isAssignCompatible(T, *VT))
        error(Node.InitialState->Loc, diag::err_value_init_type_mismatch,
              {VT->Name, T.Name});
    checkStringCapacity(T, *Node.InitialState);
    adoptSetType(*Node.InitialState, Node.ResolvedType);
}

void Sema::walkVariantFields(const VariantPart& Vp, Type& T) {
    // Tag field (the discriminator variable, e.g. 'b' in 'case b: boolean of')
    // Resolved once: an enumeration written out here declares its values, and
    // resolving the denoter a second time would declare them again.
    std::shared_ptr<Type> TagTy;
    if (!Vp.TagField.empty() && Vp.TagType) {
        TagTy = resolveType(*Vp.TagType);
        if (!std::ranges::any_of(T.RecordFields,
                [&](const Type::Field& F) { return eqCI(F.Name, Vp.TagField); }))
            T.RecordFields.push_back({ .Name = Vp.TagField, .Ty = TagTy, .IsTagField = true });
    }
    // §6.4.3.3: the case-constants of a variant part shall be distinct, for the
    // reason they must be in a case-statement — the tag value has to name one
    // variant and not two.
    std::set<int64_t> SeenTags;
    for (const auto& Vc : Vp.Cases)
        for (const auto& Lbl : Vc.Labels)
            if (Lbl)
                if (auto V = constBound(*Lbl); V && !SeenTags.insert(*V).second)
                    error(Lbl->Loc, diag::err_variant_label_duplicate,
                          {TagTy ? spellOrdinal(*TagTy, *V)
                                 : std::to_string(*V)});

    // All fixed fields from every variant case, plus recursion into nested
    // variants.
    for (const auto& Vc : Vp.Cases) {
        for (const auto& Fd : Vc.Fields) {
            auto Ft = resolveType(*Fd.Type);
            for (const auto& Nm : Fd.Names) {
                if (!std::ranges::any_of(T.RecordFields,
                        [&](const Type::Field& F) { return eqCI(F.Name, Nm); }))
                    T.RecordFields.push_back({ .Name = Nm, .Ty = Ft });
            }
        }
        if (Vc.NestedVariant) walkVariantFields(*Vc.NestedVariant, T);
    }
}

std::shared_ptr<Type> Sema::resolveTypeImpl(const TypeNode& Node) {
    if (auto* N = llvm::dyn_cast<NamedTypeNode>(&Node)) {
        return resolveNamed(*N);
    }
    if (auto* N = llvm::dyn_cast<ArrayTypeNode>(&Node)) {
        auto Elem = resolveType(*N->Element);
        // ISO §6.4.3.2: the index may be named by its type rather than written
        // as a range, in which case the extent is the whole of that type.
        if (N->Index) {
            auto IdxTy = resolveType(*N->Index);
            if (IdxTy->isError()) return TyErr;
            auto Range = ordinalRange(*IdxTy);
            if (!Range) {
                error(N->Index->Loc, diag::err_array_index_not_ordinal,
                      {IdxTy->Name});
                return TyErr;
            }
            // A subrange over the named type, so that indexing checks against
            // its bounds the same way it would for an index written as a range.
            auto Index = Ctx_.getSubrange(IdxTy, Range->first, Range->second);
            return Ctx_.getArray(Index, Elem, N->Packed);
        }
        // Determine the ordinal base type of the index from the declared bounds.
        auto Lo = checkExpr(*N->Low);
        auto Hi = checkExpr(*N->High);
        auto BaseOrd = (Lo->isOrdinal() ? Lo : (Hi->isOrdinal() ? Hi : TyInt));
        auto Bounds = foldBounds(*N->Low, *N->High, *BaseOrd,
                                 diag::err_array_lower_bound_not_const,
                                 diag::err_array_upper_bound_not_const);
        if (!Bounds) return TyErr;
        // Route index subrange through TypeContext for canonical identity.
        // Include the actual bounds so array[1..3] ≠ array[1..100].
        auto Index = Ctx_.getSubrange(BaseOrd, Bounds->first, Bounds->second);
        // Route array type through TypeContext for canonical identity.
        return Ctx_.getArray(Index, Elem, N->Packed);
    }
    if (auto* N = llvm::dyn_cast<SubrangeTypeNode>(&Node)) {
        auto Lo = checkExpr(*N->Low);
        auto Hi = checkExpr(*N->High);
        auto Base = (Lo->isOrdinal() ? Lo : (Hi->isOrdinal() ? Hi : TyInt));
        auto Bounds = foldBounds(*N->Low, *N->High, *Base,
                                 diag::err_subrange_lower_bound_not_const,
                                 diag::err_subrange_upper_bound_not_const);
        if (!Bounds) return TyErr;
        return Ctx_.getSubrange(Base, Bounds->first, Bounds->second);
    }
    if (auto* N = llvm::dyn_cast<EnumTypeNode>(&Node)) {
        auto T = std::make_shared<Type>();
        T->Kind       = TypeKind::Enum;
        T->Anonymous  = true;   // until a declaration names it
        T->Name       = describeValueList(N->Values);
        T->EnumValues = N->Values;
        // Register each value as an EnumValue symbol in the current scope.
        int Ord = 0;
        for (const auto& Val : N->Values) {
            Symbol S;
            S.Kind         = SymbolKind::EnumValue;
            S.Name         = Val;
            S.Ty         = T;
            S.OrdinalValue = Ord++;
            S.DeclLoc      = N->Loc;
            if (!Symtab.define(S))
                error(N->Loc, diag::err_duplicate_enum_value, {Val});
        }
        return T;
    }
    if (auto* N = llvm::dyn_cast<RecordTypeNode>(&Node)) {
        auto T = std::make_shared<Type>();
        T->Kind       = TypeKind::Record;
        T->Anonymous  = true;   // until a declaration names it
        T->Packed     = N->Packed;
        T->RecordDecl = N;
        for (const auto& Fd : N->Fields) {
            auto Ft = resolveType(*Fd.Type);
            for (const auto& Nm : Fd.Names) {
                if (std::ranges::any_of(T->RecordFields,
                        [&](const Type::Field& F) { return eqCI(F.Name, Nm); }))
                    error(Fd.Type->Loc, diag::err_duplicate_field, {Nm});
                else
                    T->RecordFields.push_back({ .Name = Nm, .Ty = Ft });
            }
        }
        // Walk the variant part (and nested variants) adding all variant fields to
        // RecordFields so that field-access checking can find them (ISO §6.4.3.3).
        if (N->Variant) walkVariantFields(*N->Variant, *T);
        // Named after its fields so two inline records read differently in a
        // diagnostic; nameNominalType replaces this if a declaration names it.
        std::vector<std::string> FieldNames;
        FieldNames.reserve(T->RecordFields.size());
        for (const auto& F : T->RecordFields) FieldNames.push_back(F.Name);
        T->Name = "record " + describeValueList(FieldNames);
        return T;
    }
    if (auto* N = llvm::dyn_cast<SetTypeNode>(&Node)) {
        auto Base = resolveType(*N->Base);
        if (!Base->isError() && !Base->isOrdinal())
            error(N->Loc, diag::err_set_base_not_ordinal, {Base->Name});
        else if (!Base->isError())
            checkSetBaseRange(*Base, N->Loc);
        return Ctx_.getSet(Base, N->Packed);
    }
    if (auto* N = llvm::dyn_cast<FileTypeNode>(&Node)) {
        // EP §6.4.3.6: direct-access index type
        auto Index = N->Index ? resolveType(*N->Index) : nullptr;
        std::shared_ptr<Type> Elem;
        if (N->Element) {
            Elem = resolveType(*N->Element);
            // ISO §6.4.3.5: component type must not be or contain a file type.
            if (Elem->Kind == TypeKind::File)
                error(N->Loc, diag::err_file_component_is_file);
            // EP §6.4.3.6: nor may it be a restricted type — reading one back
            // would be a way of making a value the restriction does not allow.
            else if (Elem->isRestricted())
                error(N->Loc, diag::err_restricted_file_component,
                      {Elem->Name});
            else {
                // Check for records that contain a file field
                for (const auto& F : Elem->RecordFields) {
                    if (F.Ty && F.Ty->Kind == TypeKind::File) {
                        error(N->Loc, diag::err_file_field_has_file, {F.Name});
                        break;
                    }
                }
            }
        }
        return Ctx_.getFile(std::move(Elem), std::move(Index));
    }
    if (auto* N = llvm::dyn_cast<PackedTypeNode>(&Node)) {
        return resolveType(*N->Inner);  // packed is a storage hint, not a distinct type
    }
    if (auto* N = llvm::dyn_cast<ProcedureTypeNode>(&Node)) {
        // ISO §6.6.3.1.  Not interned: §6.6.3.6 matches two of these by
        // congruity rather than identity, so a canonical instance would buy
        // nothing and the cache key would have to encode the whole signature.
        auto T  = std::make_shared<Type>();
        T->Kind = N->IsFunction ? TypeKind::Function : TypeKind::Procedure;
        for (const auto& Pg : N->Params) {
            auto Pt = resolveParamType(*Pg.Type);
            for (const auto& Nm : Pg.Names)
                T->Params.push_back({Pg.IsVar, Nm, Pt});
        }
        if (N->ReturnType) T->RetType = resolveType(*N->ReturnType);
        T->Name = describeCallable(*T);
        return T;
    }
    if (auto* N = llvm::dyn_cast<PointerTypeNode>(&Node)) {
        // EP §6.7.5.3: the domain-type of a new-pointer-type may be a bare
        // schema-name; new(p, d1..dn) supplies the discriminants.
        AllowSchemaScope Guard(AllowUndiscriminatedSchema_);
        auto Base = resolveType(*N->Base);
        return Ctx_.getPointer(Base);
    }
    if (auto* N = llvm::dyn_cast<StringTypeNode>(&Node)) {
        // EP §6.4.3.3: string(N) — variable-length string with capacity N.
        // Standard Pascal has no string type; its strings are values of
        // 'packed array [1..n] of char' (ISO §6.4.3.2).
        if (!Opts.extendedPascal()) {
            error(N->Loc, diag::err_ep_type, {"string(n)"});
            return TyErr;
        }
        auto CapTy = checkExpr(*N->Capacity);
        if (!CapTy->isError() && CapTy->Kind != TypeKind::Integer
                && !(CapTy->Kind == TypeKind::Subrange && CapTy->SubBase
                     && CapTy->SubBase->Kind == TypeKind::Integer))
            error(N->Loc, diag::err_string_cap_not_int);
        // constBound also reads schema discriminants, so `string(n)` inside a
        // schema body gets the discriminated capacity.
        // A capacity that will not fold used to become 255, which is not what
        // was written and hides the mistake behind a string that silently
        // holds the wrong amount.
        const auto Cap = constBound(*N->Capacity);
        if (!Cap) {
            if (!CapTy->isError()) error(N->Loc, diag::err_string_cap_not_int);
            return TyErr;
        }
        if (*Cap <= 0) {
            error(N->Loc, diag::err_string_cap_not_positive,
                  {std::to_string(*Cap)});
            return TyErr;
        }
        return Ctx_.getVarString(*Cap);
    }
    if (auto* N = llvm::dyn_cast<TypeOfNode>(&Node)) {
        // EP §6.4.9: type of x — resolves to the declared type of variable x.
        Symbol* Sym = Symtab.lookup(N->VarName);
        if (!Sym) {
            error(Node.Loc, diag::err_type_of_undefined, {N->VarName});
            return TyErr;
        }
        return Sym->Ty ? Sym->Ty : TyErr;
    }
    if (auto* N = llvm::dyn_cast<ConformantArrayTypeNode>(&Node)) {
        // EP §6.7.3.7: conformant array parameter type.
        // Resolve the element type (may itself be a ConformantArrayTypeNode for
        // nested dimensions produced by the abbreviated multi-dim expansion).
        auto Elem = resolveType(*N->Element);

        auto T = std::make_shared<Type>();
        T->Kind    = TypeKind::ConformantArray;
        T->ElemType = Elem;
        T->Packed   = N->Packed;

        for (const auto& Spec : N->Specs) {
            // Resolve the ordinal type name.
            std::shared_ptr<Type> OrdTy;
            {
                NamedTypeNode Nt;
                Nt.Loc  = N->Loc;
                Nt.Name = Spec.OrdType;
                OrdTy = resolveNamed(Nt);
            }
            Type::ConformantBound Cb;
            Cb.LoBoundName = Spec.Lo;
            Cb.HiBoundName = Spec.Hi;
            Cb.OrdType     = OrdTy;
            T->ConformantBounds.push_back(std::move(Cb));
        }

        // Build a display name for error messages.
        T->Name = "conformant array of " + Elem->Name;
        return T;
    }
    if (auto* N = llvm::dyn_cast<SchemaTypeNode>(&Node)) {
        // EP §6.4.8: instantiate a schema type with compile-time-constant discriminants.
        Symbol* Sym = Symtab.lookup(N->Name);
        if (!Sym || Sym->Kind != SymbolKind::Schema) {
            error(Node.Loc, diag::err_schema_not_defined, {N->Name});
            return TyErr;
        }
        // Check discriminant count.
        if (N->Actuals.size() != Sym->SchemaDeclParams.size()) {
            auto ExpStr = std::to_string(Sym->SchemaDeclParams.size());
            auto GotStr = std::to_string(N->Actuals.size());
            error(Node.Loc, diag::err_schema_wrong_arg_count, {N->Name, ExpStr, GotStr});
            return TyErr;
        }
        // Save current bindings (support nested schema instantiation).
        auto SavedBindings = ActiveSchemaBindings_;
        // Evaluate each discriminant as a compile-time integer constant.
        std::vector<Type::SchemaDisc> Discs;
        bool HasError = false;
        for (size_t I = 0; I < Sym->SchemaDeclParams.size(); ++I) {
            (void)checkExpr(*N->Actuals[I]); // type-check for diagnostics
            const auto Val = constBound(*N->Actuals[I]);
            if (!Val) {
                error(N->Actuals[I]->Loc, diag::err_schema_disc_not_const,
                      {Sym->SchemaDeclParams[I].Name});
                HasError = true;
            } else {
                Discs.push_back({.Name  = Sym->SchemaDeclParams[I].Name,
                                 .Value = *Val,
                                 .Ty    = Sym->SchemaDeclParams[I].Ty});
                ActiveSchemaBindings_[toLower(Sym->SchemaDeclParams[I].Name)] = *Val;
            }
        }
        if (HasError) {
            ActiveSchemaBindings_ = std::move(SavedBindings);
            return TyErr;
        }
        // Resolve the schema body with discriminants bound.
        auto Body = resolveType(*Sym->SchemaBodyNode);
        // Restore saved bindings.
        ActiveSchemaBindings_ = std::move(SavedBindings);

        // Build the SchemaInstance type.
        auto T = std::make_shared<Type>();
        T->Kind       = TypeKind::SchemaInstance;
        std::string Suffix = "(";
        for (size_t I = 0; I < Discs.size(); ++I) {
            if (I > 0) Suffix += ",";
            Suffix += std::to_string(Discs[I].Value);
        }
        T->Name       = N->Name + Suffix + ")";
        T->SchemaName = N->Name;
        T->SchemaDiscs = Discs;
        T->SchemaBody  = Body;
        // Cache for codegen (mutable annotation, same pattern as ExprNode::ResolvedType).
        N->ResolvedBody = T;
        return T;
    }
    error(Node.Loc, diag::err_unrecognized_type);
    return TyErr;
}

std::shared_ptr<Type> Sema::resolveNamed(const NamedTypeNode& N) {
    // EP §6.4.2.5: 'restricted T' is a type of its own.  It is made a copy of
    // T so that it is stored and lowered exactly as T is, and remembers T as
    // the type its values stand for, which is what parameter passing and a
    // function result go through.  Each mention makes its own copy — two
    // restricted denoters of one type are two types, as any two new-types are.
    if (N.Restricted) {
        auto Base = resolveNamedUnrestricted(N);
        if (Base->isError()) return Base;
        if (Base->isRestricted()) return Base; // 'restricted' twice adds nothing
        auto T = std::make_shared<Type>(*Base);
        T->RestrictedOf = Base;
        // Until a type-definition gives it a name of its own, it is known by
        // the one it restricts, which is what a reader of the program sees.
        T->Name = "restricted " + Base->Name;
        return T;
    }
    return resolveNamedUnrestricted(N);
}

std::shared_ptr<Type> Sema::resolveNamedUnrestricted(const NamedTypeNode& N) {
    // Built-in type keywords.
    std::string Lo = toLower(N.Name);

    if (Lo == "integer") return TyInt;
    if (Lo == "real")    return TyReal;
    if (Lo == "boolean") return TyBool;
    if (Lo == "char")    return TyChar;
    // Both are Extended Pascal's (EP §6.4.2.2, §6.4.3.3).  They are recognized
    // under either standard so that naming one while reading standard Pascal
    // says which type it is and where it comes from, rather than reporting an
    // undeclared name for a type plang does in fact know.
    if (Lo == "complex" || Lo == "string") {
        if (!Opts.extendedPascal()) {
            error(N.Loc, diag::err_ep_type, {Lo});
            return TyErr;
        }
        return (Lo == "complex") ? TyComplex : TyStr;
    }
    // ISO 7185 §6.4.3.5: text is a predefined file type, and one type rather
    // than a fresh one per mention — see TypeContext::getText.
    if (Lo == "text") return Ctx_.getText();

    // Look up in the symbol table.
    Symbol* Sym = Symtab.lookup(N.Name);
    if (!Sym) {
        error(N.Loc, diag::err_undefined_type, {N.Name});
        return TyErr;
    }
    // EP §6.4.7: a bare schema-name denotes a type only as a parameter-form or
    // a pointer domain-type, where the discriminants come from the actual
    // parameter or from new().  Anywhere else they have to be written out.
    if (Sym->Kind == SymbolKind::Schema) {
        if (AllowUndiscriminatedSchema_ > 0)
            return resolveUndiscriminatedSchema(*Sym, N);
        error(N.Loc, diag::err_schema_undiscriminated, {N.Name});
        return TyErr;
    }
    if (Sym->Kind != SymbolKind::TypeAlias) {
        error(N.Loc, diag::err_not_a_type, {N.Name});
        return TyErr;
    }
    return Sym->Ty ? Sym->Ty : TyErr;
}

std::shared_ptr<Type> Sema::resolveUndiscriminatedSchema(Symbol& Sym,
                                                         const NamedTypeNode& N) {
    if (!Sym.SchemaBodyNode) return TyErr;

    // One type object per schema definition, so that two spellings of `^vec`
    // give the same pointer type.  The body node address identifies the
    // definition even if the name is later shadowed.
    const std::string Key = toLower(N.Name) + "@"
                          + std::to_string(reinterpret_cast<uintptr_t>(Sym.SchemaBodyNode));
    if (auto It = UndiscSchemaTypes_.find(Key); It != UndiscSchemaTypes_.end())
        return It->second;

    // Resolve the body with the discriminants bound to a probe value.  Element
    // and field types come out right; extents do not, so we watch whether any
    // bound actually read a discriminant.  If none did, the layout is fixed and
    // the probe body describes the storage exactly.
    auto       SavedBindings = ActiveSchemaBindings_;
    const bool SavedUsed     = SchemaBindingUsed_;
    SchemaBindingUsed_ = false;
    for (const auto& P : Sym.SchemaDeclParams)
        ActiveSchemaBindings_[toLower(P.Name)] = 1;
    auto       Body         = resolveTypeImpl(*Sym.SchemaBodyNode);
    const bool LayoutVaries = SchemaBindingUsed_;
    ActiveSchemaBindings_ = std::move(SavedBindings);
    SchemaBindingUsed_    = SavedUsed;

    if (!Body || Body->isError()) return TyErr;

    // A discriminant-dependent size is only representable for an array body,
    // where each access recomputes the bounds — the schema analog of a
    // conformant array.  A record with a discriminant-sized field would need
    // run-time field offsets.
    if (LayoutVaries && Body->Kind != TypeKind::Array) {
        error(N.Loc, diag::err_schema_body_not_representable, {N.Name});
        return TyErr;
    }

    auto T = std::make_shared<Type>();
    T->Kind              = TypeKind::Schema;
    T->Name              = N.Name;
    T->SchemaName        = N.Name;
    T->SchemaBody        = Body;
    T->SchemaFixedLayout = !LayoutVaries;
    for (const auto& P : Sym.SchemaDeclParams)
        T->SchemaDiscs.push_back({.Name = P.Name, .Ty = P.Ty});

    UndiscSchemaTypes_[Key] = T;
    return T;
}

// ---------------------------------------------------------------------------
// Set base-type range checking
// ---------------------------------------------------------------------------

// See NumSemaTypeKinds in Sema/Type.h.  A new *ordinal* kind falls into the
// default below and is treated as not being a set base type at all, so the
// width check does not run for it -- which is the silent mask-truncation this
// function exists to report.  A new non-ordinal kind belongs in the default
// and needs nothing; only the count moves.
static_assert(NumSemaTypeKinds == 21,
              "a new ordinal type kind needs a case in checkSetBaseRange");

/// A set stores one bit per ordinal of its base type, so the base type's
/// ordinals must span fewer than PlangMaxSetElements values, counted from the
/// window's own origin (see setBaseOffset).  Reporting this is what keeps
/// `set of integer` from compiling into a mask that silently drops most of its
/// members.
void Sema::checkSetBaseRange(const Type& Base, SourceLocation Loc) {
    int64_t Lo = 0, Hi = 0;
    switch (Base.Kind) {
        case TypeKind::Boolean: return;              // 0..1
        case TypeKind::Char:    return;              // 0..255, exactly the width
        case TypeKind::Enum:
            Lo = 0;
            Hi = static_cast<int64_t>(Base.EnumValues.size()) - 1;
            break;
        case TypeKind::Subrange:
            Lo = Base.SubLo;
            Hi = Base.SubHi;
            // Bounds that Sema could not fold carry a node-address sentinel;
            // codegen clamps such sets at run time instead.
            if (Lo > Hi) return;
            break;
        case TypeKind::Integer:
            // Unbounded: never representable.
            error(Loc, diag::err_set_base_too_wide,
                  {Base.Name, std::to_string(PlangMaxSetElements)});
            return;
        default:
            return;
    }
    // What matters is how many ordinals the base spans, not where it starts:
    // a negative lower bound shifts the window rather than overflowing it.
    if (Hi - setBaseOffset(Base) >= PlangMaxSetElements)
        error(Loc, diag::err_set_base_too_wide,
              {Base.Name, std::to_string(PlangMaxSetElements)});
}

// ---------------------------------------------------------------------------
// constBound — Sema member implementation (declared in Sema.h)
// ---------------------------------------------------------------------------

/// Extract a compile-time integer value from a constant expression, or nothing
/// when the expression is not one.
///
/// This used to answer with the node's own address when it could not fold,
/// which reads as an ordinary bound at every call site that forgets to test
/// for it.  Two of them did, and `array[1..n]` over a variable `n` became an
/// array whose bounds were pointer bits: no diagnostic, a zero-element object,
/// and index checks comparing against nonsense.  Absence is not a number, so
/// it is no longer spelled as one.
std::optional<int64_t> Sema::constBound(const ExprNode& E) const {
    if (auto* N = llvm::dyn_cast<IntLitExpr>(&E))  return N->Value;
    if (auto* N = llvm::dyn_cast<BoolLitExpr>(&E)) return N->Value ? 1 : 0;
    // ISO §6.1.7: a one-character string is a char-type constant, so it is an
    // ordinal and may appear as an array or subrange bound.
    if (auto* N = llvm::dyn_cast<StringLitExpr>(&E))
        if (N->Value.size() == 1)
            return static_cast<int64_t>(static_cast<unsigned char>(N->Value[0]));
    if (auto* N = llvm::dyn_cast<IdentExpr>(&E)) {
        if (const Symbol* Sym = Symtab.lookup(N->Name)) {
            if (Sym->Kind == SymbolKind::EnumValue) return Sym->OrdinalValue;
            if (Sym->Kind == SymbolKind::Const && Sym->HasConstOrdinal)
                return Sym->ConstOrdinal;
        }
        // Schema discriminants stand in for constants inside a schema body.
        auto It = ActiveSchemaBindings_.find(toLower(N->Name));
        if (It != ActiveSchemaBindings_.end()) {
            SchemaBindingUsed_ = true;
            return It->second;
        }
    }
    if (auto* N = llvm::dyn_cast<UnaryExpr>(&E)) {
        if (N->Op == TokenKind::Minus)
            if (auto Inner = constBound(*N->Operand)) return -*Inner;
        if (N->Op == TokenKind::Plus)
            return constBound(*N->Operand);
    }
    // ISO §6.4.2.4: a bound is a constant-expression, so arithmetic over
    // constants folds — `array[1..2*n]` in a schema body reaches here.
    if (auto* N = llvm::dyn_cast<BinaryExpr>(&E)) {
        const auto L = constBound(*N->Left);
        const auto R = constBound(*N->Right);
        if (L && R) {
            switch (N->Op) {
            case TokenKind::Plus:  return *L + *R;
            case TokenKind::Minus: return *L - *R;
            case TokenKind::Times: return *L * *R;
            // Division by zero in a constant bound is diagnosed where the
            // expression is checked; folding it here would trap.
            case TokenKind::Div:   if (*R) return *L / *R; break;
            case TokenKind::Mod:   if (*R) return isoMod(*L, *R); break;
            // EP §6.8.3.2: an integer base keeps an integer result, so this is
            // a bound.  A negative exponent is not, and is left to the check on
            // the expression itself to report.
            case TokenKind::Pow:   if (*R >= 0) return isoPow(*L, *R); break;
            default: break;
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Storage
//
// The size of a type, worked out without a DataLayout.  See Sema::byteSizeOf
// for why Sema needs one at all and what stops it from drifting from the
// layout codegen actually emits.
// ---------------------------------------------------------------------------

namespace {

/// An integer of \p Bits occupies this many bytes, which is the next power of
/// two at or above the byte count -- what LLVM's DataLayout gives an iN and
/// what every machine plang targets does with one.
uint64_t intBytes(unsigned Bits) {
    uint64_t B = (Bits + 7) / 8;
    uint64_t P = 1;
    while (P < B) P *= 2;
    return P;
}

/// What an integer of \p Bits must be aligned to: its size, but never more
/// than sixteen.  That cap is where a set lives -- it lowers to an i256, whose
/// size is thirty-two bytes and whose alignment is sixteen on both targets
/// plang emits for.  Without the cap a record ending in a set came out eight
/// bytes short of what was laid out, in the tail padding rather than anywhere
/// a field would have shown it.
uint64_t intAlign(unsigned Bits) { return std::min<uint64_t>(intBytes(Bits), 16); }

/// Rounds \p N up to a multiple of \p A.
uint64_t roundUp(uint64_t N, uint64_t A) { return A ? (N + A - 1) / A * A : N; }

/// The storage a variant part reserves, mirroring Codegen::variantBlobType:
/// the alternatives share an array of cells whose width follows the alignment,
/// so the reservation is rounded up to a whole cell.  A record of nine bytes
/// of alternative at alignment eight reserves sixteen.
uint64_t variantBlobBytes(uint64_t Size, uint64_t Align) {
    uint64_t Unit = 1;
    if      (Align >= 8) Unit = 8;
    else if (Align >= 4) Unit = 4;
    else if (Align >= 2) Unit = 2;
    return roundUp(Size, Unit);
}

} // namespace

/// How far one alternative reaches from \p Base, and what it needs aligning
/// to.  Mirrors Codegen::Impl::layoutVariantCase: the alternatives of one
/// variant all start at the same place, and a nested variant starts after the
/// fields of the alternative containing it.
uint64_t Sema::layoutVariantCase(const VariantCase& VC, bool Packed,
                                 uint64_t Base, uint64_t& Align, bool& Ok) {
    uint64_t At = Base;
    const auto place = [&](const Type* Ft) {
        if (!Ft) { Ok = false; return; }
        const auto Sz = byteSizeOf(*Ft);
        if (!Sz) { Ok = false; return; }
        const uint64_t A = Packed ? 1 : byteAlignOf(*Ft);
        Align = std::max(Align, A);
        At    = roundUp(At, A) + *Sz;
    };

    for (const auto& Fd : VC.Fields)
        for (size_t I = 0; I < Fd.Names.size(); ++I)
            place(Fd.Type ? Fd.Type->ResolvedType.get() : nullptr);

    if (VC.NestedVariant) {
        const auto& NV = *VC.NestedVariant;
        if (!NV.TagField.empty() && NV.TagType)
            place(NV.TagType->ResolvedType.get());
        uint64_t End = At;
        for (const auto& Inner : NV.Cases)
            End = std::max(End, layoutVariantCase(Inner, Packed, At, Align, Ok));
        At = End;
    }
    return At;
}

uint64_t Sema::byteAlignOf(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Integer:
    case TypeKind::Subrange:
    case TypeKind::Enum:
    case TypeKind::Boolean:
        return intAlign(T.Width);
    case TypeKind::Char:        return 1;
    case TypeKind::Real:        return 8;
    case TypeKind::Complex:     return 8;
    case TypeKind::Set:         return intAlign(PlangMaxSetElements);
    case TypeKind::String:
    case TypeKind::Pointer:
    case TypeKind::Nil:         return 8;
    case TypeKind::VarString:   return 8;   // the length field leads it
    case TypeKind::File:        return 8;   // a pointer leads PascalFile
    case TypeKind::Array:       return T.ElemType ? byteAlignOf(*T.ElemType) : 1;
    case TypeKind::Record:
    case TypeKind::SchemaInstance: {
        // A packed record is stored wherever it will fit; nothing inside it
        // needs aligning, so neither does it.
        if (T.Packed) return 1;
        uint64_t A = 1;
        for (const auto& F : T.RecordFields)
            if (F.Ty) A = std::max(A, byteAlignOf(*F.Ty));
        return A;
    }
    default:
        return 1;
    }
}

std::optional<uint64_t> Sema::byteSizeOf(const Type& T) {
    switch (T.Kind) {
    case TypeKind::Integer:
    case TypeKind::Subrange:
    case TypeKind::Enum:
    case TypeKind::Boolean:
        return intBytes(T.Width);
    case TypeKind::Char:        return 1;
    case TypeKind::Real:        return 8;
    case TypeKind::Complex:     return 16;  // { double, double }
    case TypeKind::Set:         return intBytes(PlangMaxSetElements);
    case TypeKind::String:
    case TypeKind::Pointer:
    case TypeKind::Nil:         return 8;
    // EP §6.4.3.3: { i64 length, [capacity x i8] }.
    case TypeKind::VarString:
        return roundUp(8 + static_cast<uint64_t>(T.StrCapacity > 0 ? T.StrCapacity
                                                                   : 255), 8);
    // The PascalFile struct of Basic/PascalFileLayout.h, whose shape codegen
    // checks field by field.
    case TypeKind::File:
        return roundUp(8 + 8 + 8 + 4 + 1 + 1 + 1, 8);
    case TypeKind::Array: {
        if (!T.IndexType || !T.ElemType) return std::nullopt;
        const int64_t Count = T.IndexType->SubHi - T.IndexType->SubLo + 1;
        if (Count <= 0) return std::nullopt;
        const auto Elem = byteSizeOf(*T.ElemType);
        if (!Elem) return std::nullopt;
        return static_cast<uint64_t>(Count) * *Elem;
    }
    // A schema instance is deliberately absent.  One declaration serves every
    // instantiation and its field denoters carry the annotation of whichever
    // was resolved last, so reading a size off them gives some other
    // instance's.  The size-agreement check excludes schema bodies for the
    // same reason.
    case TypeKind::Record: {
        // Natural alignment: each field starts at a multiple of its own
        // alignment and the whole is rounded up to the widest of them.  That is
        // what plang already emits and what FPC uses by default.
        //
        // The fixed fields come from the declaration rather than from
        // RecordFields, because RecordFields is flattened: §6.4.3.3 lets a
        // variant field be selected by name like any other, so every
        // alternative's fields are in that list and summing it counts storage
        // the alternatives share.  A record of two four-byte alternatives came
        // out eight bytes here and four in the layout.
        if (!T.RecordDecl) return std::nullopt;
        const auto& RD = *T.RecordDecl;

        // ISO §6.4.3.1: a packed record is stored as economically as the
        // implementation can manage, which here means no padding between its
        // fields and none on the end of it.
        const bool Packed = RD.Packed;
        bool Ok = true;
        uint64_t Off = 0, Align = 1;
        const auto place = [&](const Type* Ft) {
            if (!Ft) { Ok = false; return; }
            const auto Sz = byteSizeOf(*Ft);
            if (!Sz) { Ok = false; return; }
            const uint64_t A = Packed ? 1 : byteAlignOf(*Ft);
            Align = std::max(Align, A);
            Off   = roundUp(Off, A) + *Sz;
        };

        for (const auto& Fd : RD.Fields)
            for (size_t I = 0; I < Fd.Names.size(); ++I)
                place(Fd.Type ? Fd.Type->ResolvedType.get() : nullptr);

        if (RD.Variant) {
            const auto& VP = *RD.Variant;
            if (!VP.TagField.empty() && VP.TagType)
                place(VP.TagType->ResolvedType.get());
            uint64_t Size = 0, BlobAlign = 1;
            for (const auto& VC : VP.Cases)
                Size = std::max(Size,
                                layoutVariantCase(VC, Packed, 0, BlobAlign, Ok));
            // Every alternative may be empty, and then there is nothing to
            // reserve: `case b: boolean of true: (); false: ()` is a record
            // with a tag and no more.
            if (Size > 0) {
                const uint64_t Blob = variantBlobBytes(Size, Packed ? 1 : BlobAlign);
                const uint64_t A    = Packed ? 1
                                             : std::min<uint64_t>(BlobAlign, 8);
                Align = std::max(Align, A);
                Off   = roundUp(Off, A) + Blob;
            }
        }
        if (!Ok) return std::nullopt;
        return roundUp(Off, Align);
    }
    // A conformant array's extent arrives with it, and an undiscriminated
    // schema's with its discriminants.  Neither has a size here to give.
    default:
        return std::nullopt;
    }
}
