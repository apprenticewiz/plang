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
    // Whether THESE bounds read a discriminant, which decides whether the
    // numbers below are this program's or the probe's.
    const bool SavedUsed = SchemaBindingUsed_;
    SchemaBindingUsed_   = false;
    const auto Lo = constBound(Low);
    const auto Hi = constBound(High);
    const bool UsedDisc = SchemaBindingUsed_;
    SchemaBindingUsed_  = SavedUsed || SchemaBindingUsed_;

    if (!Lo) error(Low.Loc,  LowID);
    if (!Hi) error(High.Loc, HighID);
    if (!Lo || !Hi) return std::nullopt;
    if (*Lo > *Hi) {
        // A schema body is resolved once with its discriminants bound to 1, to
        // get its element and field TYPES; its extents are the probe's and are
        // marked ExtentVaries so nothing uses them.  Diagnosing them was the
        // one thing that did use them, and it rejected legal programs:
        // `array[2..n]` folds to 2..1 at the probe, `array[1..n-1]` to 1..0,
        // and the message quoted bounds the user never wrote.
        //
        // Only when the bound READ a discriminant.  `array[5..2]` is empty in
        // every instantiation and is still refused here -- being wrong about
        // the probe's numbers is not a reason to stop checking constant ones.
        //
        // The real bounds are checked where they are real: each instantiation
        // resolves the body again with its own values, and t(1) for a body of
        // `array[2..n]` is refused there.
        if (ProbeBindingsActive_ && UsedDisc)
            return std::pair{*Lo, *Lo};   // a shape for the probe, not an extent
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
    stampSchemaBindings(Node, T.get());
    if (Node.InitialState) checkInitialState(Node, *T);
    return T;
}

void Sema::stampSchemaBindings(const TypeNode& Node, Type* T) const {
    if (!T || ActiveSchemaBindings_.empty() || T->Kind != TypeKind::Record)
        return;
    // Only a record built from THIS node: a named type resolves to a shared
    // type object that belongs to the declaration and not to the use, and
    // stamping that would give one instantiation's discriminants to all of them.
    if (T->RecordDecl == nullptr
            || T->RecordDecl != llvm::dyn_cast<RecordTypeNode>(&Node))
        return;
    // ExtentForm::Op::Disc (AstBase.h) indexes a discriminant POSITIONALLY,
    // by declaration order -- the same order ProbeDiscNames_ is built and
    // kept in (SchemaTypeNode resolution, above) for exactly this reason,
    // in force for every node resolved while a schema's body is being
    // resolved, however deeply nested this one is inside it. Preferred over
    // sorting ActiveSchemaBindings_'s own entries (it is an unordered_map,
    // so that would be alphabetical by name): a codegen consumer binding an
    // RtDiscScope from SchemaBindings in ITS stored order would otherwise
    // evaluate every ExtentForm here against the wrong discriminant
    // whenever the two orders disagree -- which a nested record's OWN
    // bounds do just as much as the outer body's, since both were built
    // against the same ProbeDiscNames_.
    if (!ProbeDiscNames_.empty()) {
        for (const auto& Name : ProbeDiscNames_) {
            const std::string Key = toLower(Name);
            if (auto It = ActiveSchemaBindings_.find(Key);
                    It != ActiveSchemaBindings_.end())
                T->SchemaBindings.emplace_back(Key, It->second);
        }
        return;
    }
    T->SchemaBindings.assign(ActiveSchemaBindings_.begin(),
                             ActiveSchemaBindings_.end());
    std::sort(T->SchemaBindings.begin(), T->SchemaBindings.end());
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
    // Folded HERE, in the scope the 'value' clause was actually written in --
    // constBound/constRealBound write the result onto InitialState's own
    // ConstVal/ConstRealVal.  Without this, codegen's only route to this
    // expression is emitExpr, reached through writtenInitialState's
    // Denotes-chain walk to whichever type actually carries the clause,
    // which may belong to a wholly different scope than wherever a variable
    // of it is finally declared.  emitExpr resolves an identifier against
    // whatever scope is CURRENTLY being lowered, not the one this expression
    // was written in, so `type K = integer value Foo; ... procedure Inner;
    // const Foo = 99; var n: K;` materialized Inner's unrelated Foo instead
    // of the one in scope where K's own clause was written.  Ordinal and
    // real are mutually exclusive folds, so trying both costs nothing.
    if (!constBound(*Node.InitialState))
        (void)constRealBound(*Node.InitialState);
}

void Sema::walkVariantFields(const VariantPart& Vp, Type& T) {
    // Tag field (the discriminator variable, e.g. 'b' in 'case b: boolean of')
    // Resolved once: an enumeration written out here declares its values, and
    // resolving the denoter a second time would declare them again.
    std::shared_ptr<Type> TagTy;
    if (!Vp.TagField.empty() && Vp.TagType) {
        TagTy = resolveType(*Vp.TagType);
        // §6.4.3.3: the tag field's name is a field name like any other, and
        // must be distinct from the fixed part and every earlier variant --
        // the same rule the loop below enforces for variant fields.  This
        // used to be silently SKIPPED instead of diagnosed, which dropped
        // the tag out of Sema's flattened field list while codegen still
        // laid out storage for the discriminator, so the layout cross-check
        // gate aborted the compiler with no file and no line.  A user's
        // mistake reported as an internal error is still the wrong answer.
        if (std::ranges::any_of(T.RecordFields,
                [&](const Type::Field& F) { return eqCI(F.Name, Vp.TagField); })) {
            error(Vp.TagType->Loc, diag::err_duplicate_field, {Vp.TagField});
        } else {
            T.RecordFields.push_back({ .Name = Vp.TagField, .Ty = TagTy, .IsTagField = true });
        }
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
                // §6.4.3.3: distinct across the fixed part and every variant.
                // A repeat was silently SKIPPED, which kept the first
                // declaration and left the second unreachable -- and where the
                // two alternatives put the field at different offsets, Sema's
                // flattened list and codegen's per-alternative layout gave
                // different answers and the offset gate aborted the compiler
                // with no file and no line.  A user's mistake reported as an
                // internal error is still the wrong answer.
                if (std::ranges::any_of(T.RecordFields,
                        [&](const Type::Field& F) { return eqCI(F.Name, Nm); })) {
                    error(Fd.Type->Loc, diag::err_duplicate_field, {Nm});
                    continue;
                }
                T.RecordFields.push_back({ .Name = Nm, .Ty = Ft });
            }
        }
        if (Vc.NestedVariant) walkVariantFields(*Vc.NestedVariant, T);
    }
}

std::shared_ptr<Type> Sema::resolveTypeImpl(const TypeNode& Node) {
    // Gated on having the NAMES, not on the probe being active.  A form is
    // arithmetic over discriminant INDICES, so it is the same form for every
    // instantiation -- `array[1..m]` is index 0 whether m is 1, 4 or 7 -- and
    // recording it while resolving an instantiation is therefore safe and is
    // what gives a nested schema's body forms of its own.
    if (!ProbeDiscNames_.empty()) {
        if (auto* Sn = llvm::dyn_cast<StringTypeNode>(&Node); Sn && Sn->Capacity)
            Node.ExtentLow = buildExtentForm(*Sn->Capacity, ProbeDiscNames_);
        if (auto* An = llvm::dyn_cast<ArrayTypeNode>(&Node); An && An->Low && An->High) {
            Node.ExtentLow  = buildExtentForm(*An->Low,  ProbeDiscNames_);
            Node.ExtentHigh = buildExtentForm(*An->High, ProbeDiscNames_);
        }
        if (auto* Rn = llvm::dyn_cast<SubrangeTypeNode>(&Node); Rn && Rn->Low && Rn->High) {
            Node.ExtentLow  = buildExtentForm(*Rn->Low,  ProbeDiscNames_);
            Node.ExtentHigh = buildExtentForm(*Rn->High, ProbeDiscNames_);
        }
    }

    if (auto* N = llvm::dyn_cast<NamedTypeNode>(&Node)) {
        return resolveNamed(*N);
    }
    if (auto* N = llvm::dyn_cast<ArrayTypeNode>(&Node)) {
        // EP §6.4.4 makes the pointer's IMMEDIATE domain-type the place a bare
        // schema-name may stand; a component of it is an ordinary type
        // position.  Without clearing this, `^array[1..3] of string` read its
        // component as the capacity schema, which no new() supplies.
        ClearSchemaScope NotDomain(InPointerDomain_);
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
            // The extent of a named index is the whole of its type and cannot
            // vary, but the ELEMENT's still can: `array[colour] of string(n)`
            // is fixed in count and varying in size.  Carry that up exactly as
            // the written-bounds path below does, and for the same reason --
            // uninterned, so nothing folds against the probe's element size.
            if (Elem && Elem->ExtentVaries) {
                auto T          = Ctx_.makeArrayUncached(Index, Elem, N->Packed);
                T->ExtentVaries = true;
                return T;
            }
            return Ctx_.getArray(Index, Elem, N->Packed);
        }
        // Determine the ordinal base type of the index from the declared bounds.
        auto Lo = checkExpr(*N->Low);
        auto Hi = checkExpr(*N->High);
        auto BaseOrd = (Lo->isOrdinal() ? Lo : (Hi->isOrdinal() ? Hi : TyInt));
        // As for a string capacity above: whether THESE bounds read a
        // discriminant, not whether anything in the enclosing body did.
        const bool SavedUsed = SchemaBindingUsed_;
        SchemaBindingUsed_   = false;
        auto Bounds = foldBounds(*N->Low, *N->High, *BaseOrd,
                                 diag::err_array_lower_bound_not_const,
                                 diag::err_array_upper_bound_not_const);
        const bool Varies    = SchemaBindingUsed_ && ProbeBindingsActive_;
        SchemaBindingUsed_   = SavedUsed || SchemaBindingUsed_;
        if (!Bounds) return TyErr;
        // Route index subrange through TypeContext for canonical identity.
        // Include the actual bounds so array[1..3] ≠ array[1..100].
        auto Index = Ctx_.getSubrange(BaseOrd, Bounds->first, Bounds->second);
        if (Varies || (Elem && Elem->ExtentVaries)) {
            // Not interned, for the reason the varying string is not: the
            // bounds recorded here are the probe's and must not be folded
            // against.  An element whose own extent varies carries up too --
            // `array[1..4] of string(cap)` is fixed in count and varying in
            // size, and the array is laid out at run time either way.
            auto T          = Ctx_.makeArrayUncached(Index, Elem, N->Packed);
            T->ExtentVaries = true;
            return T;
        }
        return Ctx_.getArray(Index, Elem, N->Packed);
    }
    // R3: a denoter written inside a schema body records its extents as closed
    // forms over the discriminant indices.  Built here, where the declaration
    // is, so that codegen never re-emits the expression anywhere else.

    if (auto* N = llvm::dyn_cast<SubrangeTypeNode>(&Node)) {
        auto Lo = checkExpr(*N->Low);
        auto Hi = checkExpr(*N->High);
        auto Base = (Lo->isOrdinal() ? Lo : (Hi->isOrdinal() ? Hi : TyInt));
        const bool SavedUsed = SchemaBindingUsed_;
        SchemaBindingUsed_   = false;
        auto Bounds = foldBounds(*N->Low, *N->High, *Base,
                                 diag::err_subrange_lower_bound_not_const,
                                 diag::err_subrange_upper_bound_not_const);
        const bool Varies    = SchemaBindingUsed_ && ProbeBindingsActive_;
        SchemaBindingUsed_   = SavedUsed || SchemaBindingUsed_;
        if (!Bounds) return TyErr;
        if (Varies) {
            // `record k: 1..n end`: what the discriminant fixes here is the
            // RANGE k is checked against, not any storage -- the subrange is as
            // wide as its host ordinal whatever n is.  Marked all the same, so
            // that nothing folds against the probe's bounds and the check is
            // emitted against the value the object carries.  Not interned, for
            // the same reason a varying string is not.
            auto T = std::make_shared<Type>(*Ctx_.getSubrange(Base, Bounds->first,
                                                              Bounds->second));
            T->ExtentVaries = true;
            return T;
        }
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
        ClearSchemaScope NotDomain(InPointerDomain_);   // as for an array above
        auto T = std::make_shared<Type>();
        T->Kind       = TypeKind::Record;
        T->Anonymous  = true;   // until a declaration names it
        T->Packed     = N->Packed;
        T->RecordDecl = N;
        for (const auto& Fd : N->Fields) {
            auto Ft = resolveType(*Fd.Type);
            // One field whose extent is fixed by a discriminant makes the whole
            // record's layout a run-time question: the fields after it move, and
            // so does its size.  The marker travels up so that the one test at
            // the top of a lowering says which path the record takes.
            if (Ft && Ft->ExtentVaries) T->ExtentVaries = true;
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
        // A varying extent inside a variant part moves the record's end just as
        // one in a fixed field does, and walkVariantFields adds those fields
        // without carrying the marker up -- so a schema whose only varying
        // extent was in a variant looked fixed and was laid out by the probe.
        for (const auto& F : T->RecordFields)
            if (F.Ty && F.Ty->ExtentVaries) { T->ExtentVaries = true; break; }
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
        AllowSchemaScope Domain(InPointerDomain_);
        auto Base = resolveType(*N->Base);
        return Ctx_.getPointer(Base);
        // NOTE: both guards are scoped to THIS denoter only -- see the reset in
        // the structured denoters below, which is what stops `^array[1..3] of
        // string` reading its component as the capacity schema.
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
        // Whether THIS capacity read a discriminant, as opposed to whether
        // anything in the enclosing body did: the flag is cleared around the
        // one call so the answer belongs to this extent and no other.
        const bool SavedUsed = SchemaBindingUsed_;
        SchemaBindingUsed_   = false;
        const auto Cap       = constBound(*N->Capacity);
        const bool Varies    = SchemaBindingUsed_ && ProbeBindingsActive_;
        SchemaBindingUsed_   = SavedUsed || SchemaBindingUsed_;
        if (!Cap) {
            if (!CapTy->isError()) error(N->Loc, diag::err_string_cap_not_int);
            return TyErr;
        }
        if (*Cap <= 0) {
            error(N->Loc, diag::err_string_cap_not_positive,
                  {std::to_string(*Cap)});
            return TyErr;
        }
        // A varying capacity is not interned: `string(cap)` under the probe
        // would otherwise BE `string(1)`, and every fold against the shared
        // type object would be reading the probe's answer as if it were the
        // program's.
        if (Varies) {
            auto T = Type::makeVarString(*Cap);
            T->ExtentVaries = true;
            return T;
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
        // Naming x here is a use, same as checkFor's control variable: x is
        // named directly by the type-denoter, not through an expression the
        // identifier check would otherwise see.
        Sym->Referenced = true;
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
        // On-demand resolution: an ordinary type-alias in the same block may
        // instantiate a schema (`type MyBox = Box(5);`), which resolves
        // during Sema.cpp's Phase 3b, before Phase 3b(ii)'s sweep over every
        // schema gets here.  A no-op if some earlier caller already resolved
        // Sym's discriminant params.
        resolveSchemaParams(*Sym);
        // Check discriminant count.
        if (N->Actuals.size() != Sym->SchemaDeclParams.size()) {
            auto ExpStr = std::to_string(Sym->SchemaDeclParams.size());
            auto GotStr = std::to_string(N->Actuals.size());
            error(Node.Loc, diag::err_schema_wrong_arg_count, {N->Name, ExpStr, GotStr});
            return TyErr;
        }
        // Save current bindings (support nested schema instantiation).
        auto SavedBindings = ActiveSchemaBindings_;
        // Whether folding THESE actuals reads an enclosing schema's
        // discriminant.  `inner(n)` written inside the body of `outer(n)` is
        // not a fixed instance: n is 1 only because the body is being resolved
        // against the probe, and taking that for the answer sized the object
        // for one element in every instance.  The canonical EP example --
        // matrix(m,n) = array[1..m] of vector(n) -- is exactly this shape.
        const bool SavedActualUsed = SchemaBindingUsed_;
        SchemaBindingUsed_         = false;
        // Evaluate each discriminant as a compile-time integer constant.
        std::vector<Type::SchemaDisc> Discs;
        bool HasError = false;
        for (size_t I = 0; I < Sym->SchemaDeclParams.size(); ++I) {
            auto At = checkExpr(*N->Actuals[I]); // type-check for diagnostics
            // Check ordinality first and independently of constness -- unlike
            // new()'s discriminant path (see its own comment on this exact
            // class of bug), which checks isOrdinal()/isAssignCompatible()
            // against the discriminant's own declared type, this loop used to
            // call only constBound() and accept whatever value happened to
            // fold. `Vec('a')` for `Vec(n: integer)` folded 'a' to its ordinal
            // value and sailed through uncaught; `Vec(3.5)` is a real constant
            // (not ordinal at all) but reported the misleading "must be a
            // constant expression", conflating "not constant" with "wrong
            // type". Checking ordinality up front, ahead of constBound(),
            // gives Vec(3.5) the correct wrong-type diagnostic too.
            if (At && !At->isError() && !At->isOrdinal()) {
                error(N->Actuals[I]->Loc, diag::err_schema_new_disc_type,
                      {Sym->SchemaDeclParams[I].Name, N->Name});
                HasError = true;
                continue;
            }
            const auto Val = constBound(*N->Actuals[I]);
            if (!Val) {
                error(N->Actuals[I]->Loc, diag::err_schema_disc_not_const,
                      {Sym->SchemaDeclParams[I].Name});
                HasError = true;
            } else {
                if (At && !At->isError() && Sym->SchemaDeclParams[I].Ty
                        && !Sym->SchemaDeclParams[I].Ty->isError()
                        && !isAssignCompatible(*Sym->SchemaDeclParams[I].Ty, *At)) {
                    error(N->Actuals[I]->Loc, diag::err_assign_mismatch,
                          {At->Name, Sym->SchemaDeclParams[I].Ty->Name});
                    HasError = true;
                }
                Discs.push_back({.Name  = Sym->SchemaDeclParams[I].Name,
                                 .Value = *Val,
                                 .Ty    = Sym->SchemaDeclParams[I].Ty});
                ActiveSchemaBindings_[toLower(Sym->SchemaDeclParams[I].Name)] = *Val;
            }
        }
        const bool ActualsVary = SchemaBindingUsed_ && ProbeBindingsActive_;
        // The actuals as closed forms over the ENCLOSING discriminants, so the
        // run-time walk can work out this instantiation's discriminants without
        // re-resolving a name at the allocation site.  Gated on SchemaBindingUsed_
        // alone, not ActualsVary: `outer(6)` resolved CONCRETELY (ProbeBindingsActive_
        // false) still needs x: inner(n)'s form kept over outer's own discriminants,
        // not baked to this call's n=6, because N is the one `inner(n)` syntax node
        // shared by every instantiation of outer -- only the probe pass is guaranteed
        // to run once per schema, but a program that never names outer bare never
        // takes that path at all, and the form is just as valid built from a concrete
        // pass's ProbeDiscNames_ (already in force here regardless of probing).
        if (SchemaBindingUsed_ && !ProbeDiscNames_.empty()) {
            N->ActualForms.clear();
            for (const auto& A : N->Actuals)
                if (auto F = buildExtentForm(*A, ProbeDiscNames_))
                    N->ActualForms.push_back(*F);
                else { N->ActualForms.clear(); break; }
        }
        SchemaBindingUsed_     = SavedActualUsed || SchemaBindingUsed_;
        if (HasError) {
            ActiveSchemaBindings_ = std::move(SavedBindings);
            return TyErr;
        }
        // Resolve the schema body with discriminants bound.  The names in
        // force for form-building are this schema's own, so its body records
        // extents over ITS indices -- which is what lets the run-time walk
        // evaluate them from discriminants worked out at the outer level,
        // instead of needing the inner names bound in scope.
        auto SavedFormNames = ProbeDiscNames_;
        ProbeDiscNames_.clear();
        for (const auto& P : Sym->SchemaDeclParams)
            ProbeDiscNames_.push_back(P.Name);
        // The instance's identity is settled before its body is resolved, so a
        // body that names this very instantiation -- `record next: ^t(n) end`
        // -- finds the type instead of resolving it again.  Through a POINTER
        // that is legal (ISO §6.2.2.9: a domain type may be declared later, and
        // a pointer needs no size from what it points at); the type is
        // completed in place, so the pointer ends up pointing at the finished
        // one.  Without the indirection the type contains itself and used to
        // take the compiler's stack with it.
        std::string Suffix = "(";
        for (size_t I = 0; I < Discs.size(); ++I) {
            if (I > 0) Suffix += ",";
            Suffix += std::to_string(Discs[I].Value);
        }
        Suffix += ")";
        const std::string InKey =
            std::to_string(reinterpret_cast<uintptr_t>(Sym->SchemaBodyNode))
            + Suffix;
        if (auto It = SchemaInProgress_.find(InKey); It != SchemaInProgress_.end()) {
            ProbeDiscNames_       = std::move(SavedFormNames);
            ActiveSchemaBindings_ = std::move(SavedBindings);
            if (InPointerDomain_ <= 0) {
                error(N->Loc, diag::err_schema_recursive, {N->Name});
                return TyErr;
            }
            return It->second;
        }

        auto T = std::make_shared<Type>();
        T->Kind        = TypeKind::SchemaInstance;
        T->Name        = N->Name + Suffix;
        T->SchemaName  = N->Name;
        T->SchemaDiscs = Discs;
        // WHICH schema this is an instance of.  Only the undiscriminated type
        // recorded it, so an instance had nothing but its NAME to be compared
        // by -- and two `vec(3)` from different declarations were held to be
        // the same type.
        T->SchemaBodyNode = Sym->SchemaBodyNode;
        SchemaInProgress_[InKey] = T;
        struct Leave {
            std::map<std::string, std::shared_ptr<Type>>& M; const std::string& K;
            ~Leave() { M.erase(K); }
        } LeaveGuard{SchemaInProgress_, InKey};

        // Standing in the scope the schema was DECLARED in, not the one the
        // instantiation is written in.  See SymbolTable::ScopeCeiling.
        std::shared_ptr<Type> Body;
        {
            SymbolTable::ScopeCeiling Stand(Symtab, Sym->ScopeDepth);
            Body = resolveType(*Sym->SchemaBodyNode);
        }
        ProbeDiscNames_ = std::move(SavedFormNames);
        // Restore saved bindings.
        ActiveSchemaBindings_ = std::move(SavedBindings);

        // Kind, Name, SchemaName and SchemaDiscs were filled above, before the
        // body was resolved, so a self-reference could see them.
        T->SchemaBody  = Body;
        // Marked so that nothing folds against the probe's discriminants and
        // the run-time layout is used instead -- the same marker every other
        // denoter with a discriminant-fixed extent carries.
        T->ExtentVaries = ActualsVary || (Body && Body->ExtentVaries);
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
        // EP §6.4.3.3 makes `string` a schema with one discriminant, its
        // capacity, so a bare `string` is legal exactly where any other bare
        // schema-name is -- as a pointer's domain type or a parameter's -- and
        // means the capacity arrives from new() or from the actual parameter.
        // Reading it as the unbounded String everywhere is what left
        // `new(p, 20)` for a `^string` with nowhere to put the 20.
        if (Lo == "string" && InPointerDomain_ > 0)
            return stringSchemaType();
        // A bare `string` as a VARIABLE, field, element or function result.
        // plang accepts this as an extension -- StandardGate covers it -- and
        // lowered it as the capacity-less String, which codegen emits as a raw
        // pointer: `var a: string; a := 'xy'; writeln(a)` stored a pointer and
        // printed nothing at all.
        //
        // It gets a capacity here, the widest plang has, exactly as a value
        // PARAMETER of bare `string` already did.  Refusing it instead would
        // have been defensible under §6.4.3.3 -- a bare schema-name denotes a
        // type only as a pointer's domain or a parameter's -- but it is a
        // documented extension that programs use, and making it work is a
        // smaller change than withdrawing it.
        //
        // Not in a parameter position: AllowUndiscriminatedSchema_ marks those,
        // and resolveParamType needs TyStr back to choose between the string
        // schema (var) and this same capacity (value).
        if (Lo == "string" && InPointerDomain_ == 0
                && AllowUndiscriminatedSchema_ == 0)
            return Ctx_.getVarString(PlangMaxStringCapacity);
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
    // Which declaration this name denotes, recorded in the scope the name was
    // WRITTEN in, so nothing downstream has to re-answer it by spelling.
    N.Denotes = Sym->TypeDeclNode;
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
    // Phase 3a (Sema.cpp) gives every type name a stub -- Kind=Error, Name
    // set to the type's own name -- before any body is resolved, so that a
    // POINTER may name a domain type declared later (ISO §6.2.2.9).  Reached
    // any other way, the stub means the type this name denotes has not been
    // resolved yet: `type t = record f: u end; u = integer;` looked "u" up
    // while resolving t's body, before Phase 3b ever reaches u's own
    // definition, and got the stub back silently -- a record field or array
    // element left permanently Kind=Error, with nothing to catch it until
    // codegen tried to lower it and had no LLVM type to give an "undefined
    // type" name.  EP §6.2.1(k) is explicit that this is not the relaxation
    // it sounds like: declaration PARTS may reorder, but "the prohibition of
    // forward references in declarations is retained" -- so this is refused
    // here, the same as any other undefined type, rather than silently
    // handed to whatever resolves the reference.
    if (InPointerDomain_ <= 0 && Sym->Ty && Sym->Ty->Kind == TypeKind::Error
            && !Sym->Ty->Name.empty()) {
        error(N.Loc, diag::err_forward_type_reference, {N.Name});
        return TyErr;
    }
    return Sym->Ty ? Sym->Ty : TyErr;
}

std::shared_ptr<Type> Sema::stringSchemaType() {
    // One object, so that two spellings of `^string` give one pointer type the
    // way two spellings of `^vec` do.
    if (StringSchemaTy_) return StringSchemaTy_;

    auto Body           = Type::makeVarString(1);   // the probe's answer
    Body->ExtentVaries  = true;                     // ...and not to be believed

    auto T               = std::make_shared<Type>();
    T->Kind              = TypeKind::Schema;
    T->Name              = "string";
    T->SchemaName        = "string";
    T->SchemaBody        = Body;
    T->SchemaFixedLayout = false;
    T->ExtentVaries      = true;
    T->SchemaDiscs.push_back({.Name = "capacity", .Ty = TyInt});
    StringSchemaTy_ = T;
    return T;
}

void Sema::resolveSchemaParams(Symbol& Sym) {
    // Idempotent: called both from an explicit sweep over every schema in
    // the block (Sema.cpp's Phase 3b(ii)) and on demand from here below and
    // from the SchemaTypeNode case in resolveTypeImpl, whichever reaches a
    // given schema first.  See Sema.cpp's Phase 3b(ii) comment for why no
    // single fixed position in the phase order can serve every caller.
    if (Sym.SchemaParamsResolved) return;
    Sym.SchemaParamsResolved = true;
    const TypeDef* Td = Sym.SchemaDeclTypeDef;
    if (!Td) return; // defensive: Phase 3a sets this for every Schema symbol
    for (const auto& Spec : Td->SchemaParams) {
        NamedTypeNode NtTmp;
        NtTmp.Loc  = Td->Type->Loc;
        NtTmp.Name = Spec.TypeName;
        auto ParamTy = resolveNamed(NtTmp);
        for (const auto& ParamName : Spec.Names) {
            Symbol::SchemaParam P;
            P.Name = ParamName;
            P.Ty   = ParamTy;
            Sym.SchemaDeclParams.push_back(std::move(P));
        }
    }
    Sym.SchemaBodyNode = Td->Type.get();
    Sym.DeclLoc        = Td->Type->Loc;
}

std::shared_ptr<Type> Sema::resolveUndiscriminatedSchema(Symbol& Sym,
                                                         const NamedTypeNode& N) {
    // On-demand resolution: a pointer's domain type (ISO §6.2.2.9) or a
    // formal parameter's type (EP §6.7.3.7) may name this schema before
    // Sema.cpp's Phase 3b(ii) sweep reaches it -- e.g. `type pl = ^t;
    // t(n: integer) = ...`, resolved during Phase 3b, well before that sweep
    // runs at all.  A no-op if some earlier caller already resolved it.
    resolveSchemaParams(Sym);
    if (!Sym.SchemaBodyNode) return TyErr;

    // One type object per schema definition, so that two spellings of `^vec`
    // give the same pointer type.  The body node address identifies the
    // definition even if the name is later shadowed.
    const std::string Key = toLower(N.Name) + "@"
                          + std::to_string(reinterpret_cast<uintptr_t>(Sym.SchemaBodyNode));
    if (auto It = UndiscSchemaTypes_.find(Key); It != UndiscSchemaTypes_.end())
        return It->second;

    // A schema whose body names itself is resolving its own body already.
    // Through a POINTER that is legal and ordinary -- ISO §6.2.2.9 lets a
    // domain type be declared later, and a pointer needs no size from what it
    // points at -- so the partly-built type is handed back and completed in
    // place, which leaves the pointer pointing at the finished type.  Without
    // the indirection the type contains itself, has no size, and used to take
    // the compiler's stack with it.
    if (auto It = SchemaInProgress_.find(Key); It != SchemaInProgress_.end()) {
        if (InPointerDomain_ <= 0) {
            error(N.Loc, diag::err_schema_recursive, {N.Name});
            return TyErr;
        }
        return It->second;
    }

    // Registered BEFORE the body is resolved, which is the whole point.
    auto T = std::make_shared<Type>();
    T->Kind           = TypeKind::Schema;
    T->Name           = N.Name;
    T->SchemaName     = N.Name;
    T->SchemaBodyNode = Sym.SchemaBodyNode;
    for (const auto& P : Sym.SchemaDeclParams)
        T->SchemaDiscs.push_back({.Name = P.Name, .Ty = P.Ty});
    SchemaInProgress_[Key] = T;
    struct Leave {
        std::map<std::string, std::shared_ptr<Type>>& M; const std::string& K;
        ~Leave() { M.erase(K); }
    } LeaveGuard{SchemaInProgress_, Key};

    // Resolve the body with the discriminants bound to a probe value.  Element
    // and field types come out right; extents do not, so we watch whether any
    // bound actually read a discriminant.  If none did, the layout is fixed and
    // the probe body describes the storage exactly.
    auto       SavedBindings = ActiveSchemaBindings_;
    const bool SavedUsed     = SchemaBindingUsed_;
    SchemaBindingUsed_ = false;
    const bool SavedProbe = ProbeBindingsActive_;
    ProbeBindingsActive_  = true;
    auto SavedDiscNames = ProbeDiscNames_;
    ProbeDiscNames_.clear();
    for (const auto& P : Sym.SchemaDeclParams) {
        ActiveSchemaBindings_[toLower(P.Name)] = 1;
        ProbeDiscNames_.push_back(P.Name);
    }
    auto       Body         = resolveTypeImpl(*Sym.SchemaBodyNode);
    const bool LayoutVaries = SchemaBindingUsed_;
    // The probe body is an instantiation like any other and has to say which
    // one it is.  resolveTypeImpl is called directly here -- deliberately, so
    // the probe does not overwrite the node's annotation -- and that skipped
    // the stamp, so the probe body was the one record with no bindings on it.
    // Codegen then laid it out in the empty binding context, where `string(n)`
    // folds to nothing and the field's stale annotation from a REAL
    // instantiation was the only other answer available: an internal error
    // reading "string(20) takes 264 bytes as it is written and 32 as Sema
    // resolved it" on a program that declares both `^t` and `t(20)`.
    stampSchemaBindings(*Sym.SchemaBodyNode, Body.get());
    ActiveSchemaBindings_ = std::move(SavedBindings);
    SchemaBindingUsed_    = SavedUsed;
    ProbeBindingsActive_  = SavedProbe;
    ProbeDiscNames_       = std::move(SavedDiscNames);

    if (!Body || Body->isError()) return TyErr;

    // EP §6.4.4 allows a bare schema-name as a pointer's domain type and
    // §6.7.3.7 as a parameter's type, so this restriction is plang's and not
    // the standard's -- the diagnostic says so.
    //
    // The body above was resolved ONCE against a probe binding of 1, and only
    // an array body recovers from that: schemaArrayBounds re-emits the bound
    // expressions against the discriminants new() stored in the header, so its
    // extent is right at run time.  Nothing else re-emits anything, so
    // `string(cap)` stays string(1) and `array[1..cap]` stays array[1..1], and
    // those probe extents become the GEPs, the allocation sizes and the range
    // checks.  Accepting the rest would generate wrong code, not slow code.
    //
    // All four of the things that once made this impossible now exist:
    // run-time field offsets, a run-time body size, per-field bound recovery,
    // and a Sema that marks a discriminant-dependent extent rather than
    // folding it to the probe.
    // What is left to refuse: a body that varies without any extent, range or
    // capacity of it saying so -- a discriminant read somewhere that fixes
    // neither storage nor a check.  There is nothing to compute a layout from
    // there, and nothing to check against either.
    //
    // `record k: 1..n end` used to be refused here and is not any more: the
    // storage is the host ordinal's width whatever n is, and what the
    // discriminant fixes is the RANGE k is checked against, which the run-time
    // check now reads from the object.
    // Two different questions, and conflating them is what made the old message
    // wrong.  The first is whether the body says where its extents come from:
    // a discriminant used as an extent (`string(cap)`, `array[1..n]`) marks the
    // type it sizes, and one used as anything else -- `record k: 1..n end`, where
    // what it fixes is the range k is checked against -- marks nothing and
    // leaves nothing to compute from.  That one can never be laid out.
    if (LayoutVaries && Body->Kind != TypeKind::Array && !Body->ExtentVaries) {
        error(N.Loc, diag::err_schema_body_not_representable, {N.Name});
        return TyErr;
    }
    // A variant part is laid out too: its alternatives share one run of
    // storage, so the part is as wide as the widest of them -- a max taken at
    // run time, since an alternative's own size may depend on a discriminant.

    T->SchemaBody        = Body;
    T->SchemaFixedLayout = !LayoutVaries;
    // The schema type itself says whether its body needs the run-time layout.
    // Without this the flag stopped at the body, so `p^` for an array-bodied
    // schema looked fixed and every consumer that tests the deref's type --
    // the index stride among them -- took the probe path.
    T->ExtentVaries      = Body->ExtentVaries;
    // Name, SchemaName, SchemaBodyNode and SchemaDiscs were filled before the
    // body was resolved, so a self-reference could see them.

    // R3.  An array body's bounds as closed forms over the discriminant
    // indices, folded here -- in the scope the schema was DECLARED in, which is
    // the only scope in which its bounds mean anything.  Codegen evaluates
    // these against the discriminants an object carries and never sees an
    // identifier, so there is nothing left for an allocating procedure's
    // locals to capture.
    if (const TypeNode* BodyNode = Sym.SchemaBodyNode) {
        const TypeNode* D = BodyNode;
        while (auto* Pk = llvm::dyn_cast<PackedTypeNode>(D)) D = Pk->Inner.get();
        if (auto* At = llvm::dyn_cast<ArrayTypeNode>(D); At && At->Low && At->High) {
            std::vector<std::string> Names;
            for (const auto& Dsc : T->SchemaDiscs) Names.push_back(Dsc.Name);
            T->SchemaLowForm  = buildExtentForm(*At->Low,  Names);
            T->SchemaHighForm = buildExtentForm(*At->High, Names);
        }
    }


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
std::optional<Type::ExtentForm> Sema::buildExtentForm(
        const ExprNode& E, const std::vector<std::string>& Discs) const {
    using EF = Type::ExtentForm;

    // A discriminant becomes its INDEX.  Checked before folding, because the
    // body is resolved with the discriminants bound to a probe value and
    // folding would quietly turn `n` into 1.
    if (auto* Id = llvm::dyn_cast<IdentExpr>(&E))
        for (size_t I = 0; I < Discs.size(); ++I)
            if (eqCI(Discs[I], Id->Name))
                return EF{EF::Op::Disc, static_cast<int64_t>(I), {}};

    if (auto* U = llvm::dyn_cast<UnaryExpr>(&E)) {
        if (U->Op == TokenKind::Plus) return buildExtentForm(*U->Operand, Discs);
        if (U->Op == TokenKind::Minus)
            if (auto A = buildExtentForm(*U->Operand, Discs))
                return EF{EF::Op::Neg, 0, {*A}};
    }

    if (auto* B = llvm::dyn_cast<BinaryExpr>(&E)) {
        const auto OpOf = [](TokenKind K) -> std::optional<EF::Op> {
            switch (K) {
            case TokenKind::Plus:  return EF::Op::Add;
            case TokenKind::Minus: return EF::Op::Sub;
            case TokenKind::Times: return EF::Op::Mul;
            case TokenKind::Div:   return EF::Op::Div;
            case TokenKind::Mod:   return EF::Op::Mod;
            case TokenKind::Pow:   return EF::Op::Pow;
            default:               return std::nullopt;
            }
        };
        if (auto O = OpOf(B->Op)) {
            auto L = buildExtentForm(*B->Left,  Discs);
            auto R = buildExtentForm(*B->Right, Discs);
            if (L && R) return EF{*O, 0, {*L, *R}};
        }
    }

    // Any other leaf is folded HERE, where the declaration was written, and
    // only if the fold did not read a discriminant -- a probe value is the
    // extent of no instance, and baking one in would be worse than declining.
    const bool SavedUsed = SchemaBindingUsed_;
    SchemaBindingUsed_   = false;
    const auto V         = constBound(E);
    const bool UsedProbe = SchemaBindingUsed_;
    SchemaBindingUsed_   = SavedUsed || SchemaBindingUsed_;
    if (V && !UsedProbe) return EF{EF::Op::Const, *V, {}};
    return std::nullopt;
}

std::optional<int64_t> Sema::constBound(const ExprNode& E) const {
    // Whether THIS fold read a schema discriminant, not whether anything
    // earlier did.
    const bool SavedUsed = SchemaBindingUsed_;
    SchemaBindingUsed_   = false;
    const auto V         = constBoundImpl(E);
    const bool UsedProbe = SchemaBindingUsed_;
    SchemaBindingUsed_   = SavedUsed || SchemaBindingUsed_;
    // Recorded for codegen, except where the value came from a probe binding:
    // a bound over a discriminant is a different constant in every instance,
    // and the one folded here belongs to none of them.
    if (V && !UsedProbe) E.ConstVal = V;
    return V;
}

std::optional<int64_t> Sema::constBoundImpl(const ExprNode& E) const {
    if (auto* N = llvm::dyn_cast<IntLitExpr>(&E))  return N->Value;
    if (auto* N = llvm::dyn_cast<BoolLitExpr>(&E)) return N->Value ? 1 : 0;
    // ISO §6.1.7: a one-character string is a char-type constant, so it is an
    // ordinal and may appear as an array or subrange bound.
    if (auto* N = llvm::dyn_cast<StringLitExpr>(&E))
        if (N->Value.size() == 1)
            return static_cast<int64_t>(static_cast<unsigned char>(N->Value[0]));
    if (auto* N = llvm::dyn_cast<IdentExpr>(&E)) {
        // A schema's formal discriminants are declared by the schema, and the
        // schema's region encloses its body, so inside the body a discriminant
        // SHADOWS anything of the same spelling outside it (ISO 10206 §6.2.2).
        // This was asked second, so a `const n = 3` beside `type t(n: integer)
        // = array[1..n] of integer` beat the discriminant: every t was three
        // elements long whatever new() was told, and the heap went with it.
        auto It = ActiveSchemaBindings_.find(toLower(N->Name));
        if (It != ActiveSchemaBindings_.end()) {
            SchemaBindingUsed_ = true;
            return It->second;
        }
        if (const Symbol* Sym = Symtab.lookup(N->Name)) {
            if (Sym->Kind == SymbolKind::EnumValue) return Sym->OrdinalValue;
            if (Sym->Kind == SymbolKind::Const && Sym->HasConstOrdinal)
                return Sym->ConstOrdinal;
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
    // EP §6.8.2: a call to a required function is nonvarying (and so may
    // appear in a constant-expression, and by the identical two-part test in
    // §6.4.2.4, in a bound) unless it is eof or eoln -- the ONE exclusion
    // §6.8.2(c) carves out.  §6.3.2's own constant-definition examples call
    // arctan and index this way ("pi = 4 * arctan(1);").  Scoped here to the
    // ordinal-domain functions this int64 fold already has a value space
    // for: each reduces to one recursive fold of its own argument(s), so a
    // real-valued argument -- nothing here folds a real constant -- simply
    // fails to fold, same as it did before this call existed.
    if (auto* N = llvm::dyn_cast<CallExpr>(&E)) {
        switch (N->ResolvedBuiltin) {
        case BuiltinID::Abs:
            if (auto V = constBound(*N->Args[0])) return *V < 0 ? -*V : *V;
            break;
        case BuiltinID::Sqr:
            if (auto V = constBound(*N->Args[0])) return *V * *V;
            break;
        // ord and chr both represent their value as its plain ordinal here,
        // the same as the single-character StringLitExpr case above.
        case BuiltinID::Ord:
        case BuiltinID::Chr:
            return constBound(*N->Args[0]);
        case BuiltinID::Odd:
            if (auto V = constBound(*N->Args[0])) return (*V & 1) != 0;
            break;
        // EP §6.7.6.5's second argument is the step; ISO 7185's one-argument
        // form is this with an implicit step of 1.
        case BuiltinID::Succ:
        case BuiltinID::Pred: {
            const auto V = constBound(*N->Args[0]);
            const auto K = N->Args.size() > 1 ? constBound(*N->Args[1])
                                               : std::optional<int64_t>(1);
            if (V && K)
                return N->ResolvedBuiltin == BuiltinID::Succ ? *V + *K : *V - *K;
            break;
        }
        default: break;
        }
    }
    return std::nullopt;
}

// constBound's real-valued sibling.  Deliberately narrow -- see
// ExprNode::ConstRealVal's comment for why this exists at all: it is not a
// general real evaluator, only enough to fold a 'value' clause's real-valued
// initializer where Sema checks it, in the scope it was written in.
std::optional<double> Sema::constRealBound(const ExprNode& E) const {
    if (auto* N = llvm::dyn_cast<RealLitExpr>(&E)) {
        E.ConstRealVal = N->Value;
        return N->Value;
    }
    // ISO §6.4.6: integer widens to real, so `real value 5` (a literal) and
    // `real value SomeIntConst` (a named ordinal constant) fold too.
    if (auto* N = llvm::dyn_cast<IntLitExpr>(&E)) {
        const double V = static_cast<double>(N->Value);
        E.ConstRealVal = V;
        return V;
    }
    if (auto* N = llvm::dyn_cast<IdentExpr>(&E)) {
        if (const Symbol* Sym = Symtab.lookup(N->Name);
                Sym && Sym->Kind == SymbolKind::Const) {
            if (Sym->HasConstReal) {
                E.ConstRealVal = Sym->ConstReal;
                return Sym->ConstReal;
            }
            if (Sym->HasConstOrdinal) {
                const double V = static_cast<double>(Sym->ConstOrdinal);
                E.ConstRealVal = V;
                return V;
            }
        }
    }
    if (auto* N = llvm::dyn_cast<UnaryExpr>(&E)) {
        if (N->Op == TokenKind::Minus) {
            if (auto Inner = constRealBound(*N->Operand)) {
                E.ConstRealVal = -*Inner;
                return -*Inner;
            }
        } else if (N->Op == TokenKind::Plus) {
            if (auto Inner = constRealBound(*N->Operand)) {
                E.ConstRealVal = *Inner;
                return *Inner;
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
    if      (Align >= 16) Unit = 16;
    else if (Align >= 8)  Unit = 8;
    else if (Align >= 4)  Unit = 4;
    else if (Align >= 2)  Unit = 2;
    return roundUp(Size, Unit);
}

} // namespace

/// How far one alternative reaches from \p Base, and what it needs aligning
/// to.  Mirrors Codegen::Impl::layoutVariantCase: the alternatives of one
/// variant all start at the same place, and a nested variant starts after the
/// fields of the alternative containing it.
uint64_t Sema::layoutVariantCase(const VariantCase& VC, bool Packed,
                                 uint64_t Base, uint64_t& Align, bool& Ok,
                                 FieldOffsets* Offsets) {
    uint64_t At = Base;
    const auto place = [&](const Type* Ft, const std::string* Name) {
        if (!Ft) { Ok = false; return; }
        const auto Sz = byteSizeOf(*Ft);
        if (!Sz) { Ok = false; return; }
        const uint64_t A = Packed ? 1 : byteAlignOf(*Ft);
        Align = std::max(Align, A);
        At    = roundUp(At, A);
        // Recorded RELATIVE to the start of the shared run; byteSizeOf adds
        // where that run begins once it knows, since the run's alignment is
        // not settled until every alternative has been walked.
        if (Offsets && Name) Offsets->emplace_back(*Name, At);
        At += *Sz;
    };

    for (const auto& Fd : VC.Fields)
        for (size_t I = 0; I < Fd.Names.size(); ++I)
            place(Fd.Type ? Fd.Type->ResolvedType.get() : nullptr,
                  &Fd.Names[I]);

    if (VC.NestedVariant) {
        const auto& NV = *VC.NestedVariant;
        if (!NV.TagField.empty() && NV.TagType)
            place(NV.TagType->ResolvedType.get(), &NV.TagField);
        uint64_t End = At;
        for (const auto& Inner : NV.Cases)
            End = std::max(End,
                           layoutVariantCase(Inner, Packed, At, Align, Ok, Offsets));
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

std::optional<uint64_t> Sema::byteSizeOf(const Type& T, FieldOffsets* Offsets) {
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
        const auto place = [&](const Type* Ft, const std::string* Name) {
            if (!Ft) { Ok = false; return; }
            const auto Sz = byteSizeOf(*Ft);
            if (!Sz) { Ok = false; return; }
            const uint64_t A = Packed ? 1 : byteAlignOf(*Ft);
            Align = std::max(Align, A);
            Off   = roundUp(Off, A);
            // R4: the offsets fall out of the walk that computes the size, and
            // are handed to whoever asked so that codegen can be CHECKED
            // against them.  Only the total was ever compared before, and a
            // record can be the right size with every field in the wrong place.
            if (Offsets && Name) Offsets->emplace_back(*Name, Off);
            Off += *Sz;
        };

        for (const auto& Fd : RD.Fields)
            for (size_t I = 0; I < Fd.Names.size(); ++I)
                place(Fd.Type ? Fd.Type->ResolvedType.get() : nullptr,
                      &Fd.Names[I]);

        if (RD.Variant) {
            const auto& VP = *RD.Variant;
            if (!VP.TagField.empty() && VP.TagType)
                place(VP.TagType->ResolvedType.get(), &VP.TagField);
            uint64_t Size = 0, BlobAlign = 1;
            const size_t FirstVariantEntry = Offsets ? Offsets->size() : 0;
            for (const auto& VC : VP.Cases)
                Size = std::max(Size,
                                layoutVariantCase(VC, Packed, 0, BlobAlign, Ok,
                                                  Offsets));
            // Every alternative may be empty, and then there is nothing to
            // reserve: `case b: boolean of true: (); false: ()` is a record
            // with a tag and no more.
            if (Size > 0) {
                const uint64_t Blob = variantBlobBytes(Size, Packed ? 1 : BlobAlign);
                // Not clamped to 8.  It was, mirroring a cap in
                // Codegen::variantBlobType that has gone: a part holding a
                // `set of char` needs 16, and giving the run 8 put the set at
                // an offset its own type forbids while codegen went on
                // emitting `align 16` accesses to it.
                const uint64_t A    = Packed ? 1 : BlobAlign;
                Align = std::max(Align, A);
                Off   = roundUp(Off, A);
                // Where the shared run begins is known only now, so the
                // alternatives' offsets are shifted onto it.
                if (Offsets)
                    for (size_t I = FirstVariantEntry; I < Offsets->size(); ++I)
                        (*Offsets)[I].second += Off;
                Off  += Blob;
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
