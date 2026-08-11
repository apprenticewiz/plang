#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <format>
#include <ranges>
#include <span>

using namespace plang;

// See NumExprKinds in AstBase.h.
static_assert(NumExprKinds == 16, "a new expression needs a case in checkExpr");

// ---------------------------------------------------------------------------
// Comparisons that one operand's type has already settled
// ---------------------------------------------------------------------------

void Sema::warnIfComparisonIsSettled(const BinaryExpr& E, const Type& Lt,
                                      const Type& Rt) {
    // One side has to be a constant and the other a variable whose type
    // carries a range.  Naming the variable is what makes the message worth
    // reading, so only an identifier qualifies as the variable side.
    const IdentExpr* Var = nullptr;
    const Type*      VarTy = nullptr;
    std::optional<int64_t> K;
    TokenKind Op = E.Op;

    if (auto V = constBound(*E.Right); V && !constBound(*E.Left)) {
        Var = llvm::dyn_cast<IdentExpr>(E.Left.get());
        VarTy = &Lt;
        K = V;
    } else if (auto V = constBound(*E.Left); V && !constBound(*E.Right)) {
        Var = llvm::dyn_cast<IdentExpr>(E.Right.get());
        VarTy = &Rt;
        K = V;
        // Read the comparison from the variable's side instead.
        switch (Op) {
        case TokenKind::LessThan:           Op = TokenKind::GreaterThan;        break;
        case TokenKind::GreaterThan:        Op = TokenKind::LessThan;           break;
        case TokenKind::LessThanOrEqual:    Op = TokenKind::GreaterThanOrEqual; break;
        case TokenKind::GreaterThanOrEqual: Op = TokenKind::LessThanOrEqual;    break;
        default: break;
        }
    }
    if (!Var || !K) return;

    // A range the program did not write down is not evidence of a mistake.
    // Every char is within chr(0)..chr(255) and every boolean within
    // false..true, so admitting those would flag code that is simply correct.
    if (VarTy->Kind != TypeKind::Subrange && VarTy->Kind != TypeKind::Enum)
        return;
    auto R = ordinalRange(*VarTy);
    if (!R) return;
    const auto [Lo, Hi] = *R;
    if (Lo > Hi) return;

    // What the range settles, if anything.  Each answer holds for every value
    // the variable can take, which is what makes the other operand redundant.
    std::optional<bool> Always;
    switch (Op) {
    case TokenKind::Equal:
        if (*K < Lo || *K > Hi)          Always = false;
        else if (Lo == Hi)               Always = true;
        break;
    case TokenKind::NotEqual:
        if (*K < Lo || *K > Hi)          Always = true;
        else if (Lo == Hi)               Always = false;
        break;
    case TokenKind::LessThan:
        if (*K > Hi)                     Always = true;
        else if (*K <= Lo)               Always = false;
        break;
    case TokenKind::LessThanOrEqual:
        if (*K >= Hi)                    Always = true;
        else if (*K < Lo)                Always = false;
        break;
    case TokenKind::GreaterThan:
        if (*K < Lo)                     Always = true;
        else if (*K >= Hi)               Always = false;
        break;
    case TokenKind::GreaterThanOrEqual:
        if (*K <= Lo)                    Always = true;
        else if (*K > Hi)                Always = false;
        break;
    default:
        return;
    }
    if (!Always) return;

    warning(E.Loc, diag::warn_compare_always,
            {*Always ? "true" : "false", Var->Name, VarTy->Name,
             spellOrdinal(*VarTy, Lo), spellOrdinal(*VarTy, Hi)});
}

// ---------------------------------------------------------------------------
// Expression checking
// ---------------------------------------------------------------------------

std::shared_ptr<Type> Sema::checkExpr(const ExprNode& E) {
    std::shared_ptr<Type> T;

    if (llvm::dyn_cast<IntLitExpr>(&E))
        T = TyInt;
    else if (llvm::dyn_cast<RealLitExpr>(&E))
        T = TyReal;
    else if (llvm::dyn_cast<NilExpr>(&E))
        T = TyNil;
    else if (llvm::dyn_cast<BoolLitExpr>(&E))
        T = TyBool;
    else if (auto* N = llvm::dyn_cast<StringLitExpr>(&E)) {
        // ISO §6.1.7 requires at least one string-element, but EP §6.1.8 adds
        // the zero-length string: '' is how a string variable is cleared, and
        // how a recursive string function reaches its base case.
        if (N->Value.empty() && !Opts.has(LangOptions::Feature::EmptyStringLiteral))
            error(E.Loc, diag::err_empty_string_const);
        if (N->Value.empty())
            T = Type::makeVarString(0);
        else if (N->Value.size() == 1)
            T = TyChar;
        else if (Opts.extendedPascal())
            // EP: string literals carry their length as capacity for concatenation.
            T = Type::makeVarString(static_cast<int64_t>(N->Value.size()));
        else
            T = TyStr;
    }
    else if (auto* N = llvm::dyn_cast<IdentExpr>(&E))      T = checkIdent(*N);
    else if (auto* N = llvm::dyn_cast<IndexExpr>(&E))      T = checkIndex(*N);
    else if (auto* N = llvm::dyn_cast<FieldExpr>(&E))      T = checkField(*N);
    else if (auto* N = llvm::dyn_cast<DerefExpr>(&E))      T = checkDeref(*N);
    else if (auto* N = llvm::dyn_cast<BinaryExpr>(&E))     T = checkBinary(*N);
    else if (auto* N = llvm::dyn_cast<UnaryExpr>(&E))      T = checkUnary(*N);
    else if (auto* N = llvm::dyn_cast<CallExpr>(&E))       T = checkCallExpr(*N);
    else if (auto* N = llvm::dyn_cast<SetLiteralExpr>(&E)) T = checkSetLit(*N);
    else if (auto* N = llvm::dyn_cast<StructuredValueExpr>(&E)) T = checkStructuredValue(*N);
    else if (auto* N = llvm::dyn_cast<WriteParam>(&E)) {
        T = checkExpr(*N->Value);
        if (N->Width)    (void)checkExpr(*N->Width);
        if (N->Decimals) (void)checkExpr(*N->Decimals);
    }
    else if (auto* N = llvm::dyn_cast<SetRangeExpr>(&E)) {
        (void)checkExpr(*N->Low);
        (void)checkExpr(*N->High);
        T = TyErr; // not expected at top level
    }
    else if (auto* N = llvm::dyn_cast<SubstringExpr>(&E)) {
        // EP §6.5.6: s[i..j] — result type is the source string type.
        T = checkExpr(*N->Str);
        (void)checkExpr(*N->Low);
        (void)checkExpr(*N->High);
        if (T->Kind != TypeKind::VarString)
            error(E.Loc, diag::err_substring_non_varstring);
    }
    else {
        error(E.Loc, diag::err_unrecognized_expr);
        T = TyErr;
    }

    E.ResolvedType = T;
    return T;
}

std::shared_ptr<Type> Sema::checkIdent(const IdentExpr& E) {
    // Inside a function body, the function's own name (or named result variable,
    // EP §6.7.2) is the result pseudo-variable.
    if (CurrentProc && CurrentProc->IsFunction && CurrentRetType) {
        if (eqCI(E.Name, CurrentProc->Name)) return CurrentRetType;
        if (!CurrentProc->ResultName.empty() &&
            eqCI(E.Name, CurrentProc->ResultName)) return CurrentRetType;
    }

    // EP §6.4.7: active schema discriminant bindings — treated as integer constants.
    if (!ActiveSchemaBindings_.empty()) {
        auto It = ActiveSchemaBindings_.find(toLower(E.Name));
        if (It != ActiveSchemaBindings_.end())
            return TyInt;
    }

    Symbol* Sym = Symtab.lookup(E.Name);
    if (!Sym) {
        error(E.Loc, diag::err_undefined_identifier, {E.Name});
        return TyErr;
    }
    Sym->Referenced = true;
    switch (Sym->Kind) {
        case SymbolKind::Var:
        case SymbolKind::VarParam:
        case SymbolKind::Const:
        case SymbolKind::EnumValue:
            return Sym->Ty ? Sym->Ty : TyErr;
        case SymbolKind::Proc:
            if (Sym->IsFunction) {
                if (!Sym->Params.empty()) {
                    error(E.Loc, diag::err_function_requires_args, {E.Name});
                }
                return Sym->ReturnType ? Sym->ReturnType : TyErr;
            }
            error(E.Loc, diag::err_proc_as_value, {E.Name});
            return TyErr;
        case SymbolKind::Builtin:
            if (Sym->IsFunction)
                return Sym->ReturnType ? Sym->ReturnType : TyErr;
            error(E.Loc, diag::err_builtin_proc_as_value, {E.Name});
            return TyErr;
        case SymbolKind::TypeAlias:
        case SymbolKind::Schema:  // EP §6.4.7: schema name cannot be used as a value
            error(E.Loc, diag::err_type_name_as_value, {E.Name});
            return TyErr;
        case SymbolKind::Label:
            error(E.Loc, diag::err_label_as_value, {E.Name});
            return TyErr;
    }
    return TyErr;
}

// EP §6.4.2.5: the structure of a restricted type is not there to be reached
// into — its components are what the restriction hides.  Reports the attempt
// and answers whether one was made.
bool Sema::rejectRestrictedComponent(const ExprNode& E, const Type& T) {
    if (!T.isRestricted()) return false;
    error(E.Loc, diag::err_restricted_component, {T.Name});
    return true;
}

std::shared_ptr<Type> Sema::checkIndex(const IndexExpr& E) {
    auto ArrTy = checkExpr(*E.Array);
    auto IdxTy = checkExpr(*E.Index);
    if (ArrTy->isError()) return TyErr;
    if (rejectRestrictedComponent(E, *ArrTy)) return TyErr;
    // EP §6.4.7: a schema is indexed through its body.  An undiscriminated one
    // gives the element type but not the bounds — those come from the
    // discriminants at run time, so skip the static index-range check below.
    const bool Undiscriminated = ArrTy->Kind == TypeKind::Schema
                                 && !ArrTy->SchemaFixedLayout;
    if (ArrTy->Kind == TypeKind::SchemaInstance || ArrTy->Kind == TypeKind::Schema) {
        if (!ArrTy->SchemaBody) {
            error(E.Loc, diag::err_subscript_non_array, {ArrTy->Name});
            return TyErr;
        }
        ArrTy = ArrTy->SchemaBody;
    }
    if (Undiscriminated) {
        if (ArrTy->Kind != TypeKind::Array) {
            error(E.Loc, diag::err_subscript_non_array, {ArrTy->Name});
            return TyErr;
        }
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return ArrTy->ElemType ? ArrTy->ElemType : TyErr;
    }
    // EP §6.5.3.2: a string has char components selectable by index, numbered
    // from 1 up to its current length.
    if (ArrTy->Kind == TypeKind::VarString) {
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return TyChar;
    }
    // EP §6.7.3.7: conformant arrays are indexed like regular arrays.
    if (ArrTy->Kind == TypeKind::ConformantArray) {
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return ArrTy->ElemType ? ArrTy->ElemType : TyErr;
    }
    if (ArrTy->Kind != TypeKind::Array) {
        error(E.Loc, diag::err_subscript_non_array, {ArrTy->Name});
        return TyErr;
    }
    if (!IdxTy->isError() && !IdxTy->isOrdinal())
        error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
    // ISO §6.5.3.2: index expression must be assignment-compatible with the declared index type.
    if (!IdxTy->isError() && ArrTy->IndexType && !ArrTy->IndexType->isError()
        && !isAssignCompatible(*ArrTy->IndexType, *IdxTy))
        error(E.Loc, diag::err_index_type_mismatch,
              {IdxTy->Name, ArrTy->IndexType->Name});
    return ArrTy->ElemType ? ArrTy->ElemType : TyErr;
}

std::shared_ptr<Type> Sema::checkField(const FieldExpr& E) {
    auto RecTy = checkExpr(*E.Record);
    if (RecTy->isError()) return TyErr;
    if (rejectRestrictedComponent(E, *RecTy)) return TyErr;

    // EP §6.8.4: `v.d` on a schematic variable is a schema-discriminant, so it
    // takes precedence over any body field of the same name.
    if (RecTy->Kind == TypeKind::SchemaInstance || RecTy->Kind == TypeKind::Schema) {
        const bool Undiscriminated = RecTy->Kind == TypeKind::Schema;
        for (const auto& D : RecTy->SchemaDiscs) {
            if (eqCI(D.Name, E.Field))
                return D.Ty && !D.Ty->isError() ? D.Ty : TyInt;
        }
        // Not a discriminant — check the underlying body type.
        if (!RecTy->SchemaBody) {
            error(E.Loc, diag::err_field_on_non_record, {E.Field, RecTy->Name});
            return TyErr;
        }
        // Without the discriminants the body's field offsets are only known
        // when the layout is fixed; resolveUndiscriminatedSchema has already
        // rejected the varying non-array case, so this is safe.
        if (Undiscriminated && RecTy->SchemaBody->Kind != TypeKind::Record) {
            error(E.Loc, diag::err_schema_not_a_discriminant, {RecTy->Name, E.Field});
            return TyErr;
        }
        RecTy = RecTy->SchemaBody;
    }

    if (RecTy->Kind != TypeKind::Record) {
        error(E.Loc, diag::err_field_on_non_record, {E.Field, RecTy->Name});
        return TyErr;
    }
    const Type::Field* F = RecTy->fieldByName(E.Field);
    if (!F) {
        error(E.Loc, diag::err_no_such_field, {RecTy->Name, E.Field});
        return TyErr;
    }
    return F->Ty ? F->Ty : TyErr;
}

std::shared_ptr<Type> Sema::checkDeref(const DerefExpr& E) {
    auto PtrTy = checkExpr(*E.Pointer);
    if (PtrTy->isError()) return TyErr;
    if (rejectRestrictedComponent(E, *PtrTy)) return TyErr;
    if (PtrTy->Kind == TypeKind::Pointer)
        return PtrTy->PointeeType ? PtrTy->PointeeType : TyErr;
    if (PtrTy->Kind == TypeKind::File) {
        // ISO §6.5.5: f^ for a file variable accesses the file buffer variable.
        // Its type is the file's component type; for text files it is char.
        return PtrTy->ElemType ? PtrTy->ElemType : TyChar;
    }
    error(E.Loc, diag::err_deref_non_pointer, {PtrTy->Name});
    return TyErr;
}

std::shared_ptr<Type> Sema::checkBinary(const BinaryExpr& E) {
    auto Lt = checkExpr(*E.Left);
    auto Rt = checkExpr(*E.Right);
    if (Lt->isError() || Rt->isError()) return TyErr;

    // EP §6.4.2.5: no operator applies to a restricted value, comparison for
    // equality included — the values are opaque, so there is nothing an
    // operator could be said to do with them.
    if (Lt->isRestricted() || Rt->isRestricted()) {
        error(E.Loc, diag::err_restricted_used,
              {Lt->isRestricted() ? Lt->Name : Rt->Name});
        return TyErr;
    }

    switch (E.Op) {
        case TokenKind::Plus:
            // EP §6.8.3.6: string concatenation.
            if (Lt->Kind == TypeKind::VarString || Rt->Kind == TypeKind::VarString
                || Lt->Kind == TypeKind::String  || Rt->Kind == TypeKind::String
                || (Lt->Kind == TypeKind::Char && (Rt->Kind == TypeKind::VarString
                                                 || Rt->Kind == TypeKind::String))
                || (Rt->Kind == TypeKind::Char && (Lt->Kind == TypeKind::VarString
                                                 || Lt->Kind == TypeKind::String))
                // EP §6.8.3.2: a char is a string-compatible operand of '+', so
                // two of them concatenate rather than failing as non-numeric.
                || (Opts.has(LangOptions::Feature::CharConcatenation)
                        && Lt->Kind == TypeKind::Char
                        && Rt->Kind == TypeKind::Char)) {
                auto cap = [](const std::shared_ptr<Type>& T) -> int64_t {
                    if (T->Kind == TypeKind::VarString) return T->StrCapacity;
                    if (T->Kind == TypeKind::Char)      return 1;
                    return PlangMaxStringCapacity; // unbounded string
                };
                return Type::makeVarString(cap(Lt) + cap(Rt));
            }
            [[fallthrough]];
        case TokenKind::Minus:
        case TokenKind::Times:
            // ISO §6.7.2.4: '+' is set union, '-' set difference and '*' set
            // intersection when both operands are sets.
            if (Lt->Kind == TypeKind::Set || Rt->Kind == TypeKind::Set) {
                if (Lt->Kind != TypeKind::Set || Rt->Kind != TypeKind::Set) {
                    error(E.Loc, diag::err_op_set_mixed,
                          {opSpelling(E.Op), Lt->Name, Rt->Name});
                    return TyErr;
                }
                // Unify before comparing bases: a set-constructor's element
                // type is whatever its elements happened to be, so comparing
                // it against the other operand would reject `s + [1]`.
                unifyLooseSets(*E.Left, *E.Right, Lt, Rt);
                const auto& Lu = E.Left->ResolvedType;
                const auto& Ru = E.Right->ResolvedType;
                // The empty set literal [] has no element type and unifies
                // with any set, so only compare when both bases are known.
                if (Lu->ElemType && Ru->ElemType
                    && !Lu->ElemType->isError() && !Ru->ElemType->isError()
                    && !isAssignCompatible(*Lu->ElemType, *Ru->ElemType)) {
                    error(E.Loc, diag::err_op_set_base,
                          {opSpelling(E.Op), Lu->ElemType->Name, Ru->ElemType->Name});
                    return TyErr;
                }
                // Both operands agree now; the definite one names the result.
                return isLooseSet(*E.Left) ? Ru : Lu;
            }
            if (!Lt->isNumeric() || !Rt->isNumeric()) {
                error(E.Loc, diag::err_op_numeric,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            return numericResult(*Lt, *Rt);

        case TokenKind::Divide:
            if (!Lt->isNumeric() || !Rt->isNumeric()) {
                error(E.Loc, diag::err_op_divide_numeric, {Lt->Name, Rt->Name});
                return TyErr;
            }
            if (auto R = constBound(*E.Right); R && *R == 0)
                warning(E.Loc, diag::warn_const_div_zero, {opSpelling(E.Op)});
            // EP §6.8.3.2: complex / anything → complex; otherwise real.
            if (Lt->Kind == TypeKind::Complex || Rt->Kind == TypeKind::Complex)
                return TyComplex;
            return TyReal; // division always yields real

        case TokenKind::Div:
        case TokenKind::Mod:
            if (!Lt->isIntegral() || !Rt->isIntegral()) {
                error(E.Loc, diag::err_op_integer,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            if (auto R = constBound(*E.Right); R && *R == 0)
                warning(E.Loc, diag::warn_const_div_zero, {opSpelling(E.Op)});
            return TyInt;

        case TokenKind::And:
        case TokenKind::Or:
        case TokenKind::AndThen:  // EP §6.8.3.3
        case TokenKind::OrElse:   // EP §6.8.3.3
            if (Lt->Kind != TypeKind::Boolean || Rt->Kind != TypeKind::Boolean) {
                error(E.Loc, diag::err_op_boolean,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            return TyBool;

        case TokenKind::StarStar: // EP §6.8.3.2: ** — always a real result
        case TokenKind::Pow:      // EP §6.8.3.2: pow — result follows the base
            if (!Lt->isNumeric() || !Rt->isNumeric()) {
                error(E.Loc, diag::err_op_numeric,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            if (Lt->Kind == TypeKind::Complex || Rt->Kind == TypeKind::Complex)
                return TyComplex;
            // EP §6.8.3.2: 'pow' takes an integer exponent and yields the type
            // of its base, so an integer raised to a power stays an integer
            // rather than making a round trip through double.
            if (E.Op == TokenKind::Pow && Lt->isOrdinal() && Rt->isOrdinal())
                return TyInt;
            return TyReal;

        case TokenKind::SymDiff:  // EP §6.8.3.4: >< — symmetric set difference
            if (Lt->Kind != TypeKind::Set || Rt->Kind != TypeKind::Set) {
                error(E.Loc, diag::err_op_symdiff_set, {Lt->Name, Rt->Name});
                return TyErr;
            }
            unifyLooseSets(*E.Left, *E.Right, Lt, Rt);
            return isLooseSet(*E.Left) ? E.Right->ResolvedType
                                       : E.Left->ResolvedType;

        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::LessThan:
        case TokenKind::LessThanOrEqual:
        case TokenKind::GreaterThan:
        case TokenKind::GreaterThanOrEqual: {
            // ISO §6.4.3.2 makes a packed array[1..n] of char a string value,
            // and §6.7.2.5 lets string values be compared.
            auto isStringLike = [](const Type& T) {
                return T.Kind == TypeKind::String || T.Kind == TypeKind::VarString
                    || T.Kind == TypeKind::Char   || isCharStringType(T);
            };
            // ISO §6.7.2.5: sets support = <> <= >= only; '<' and '>' are not
            // set operators, and comparing a set against a non-set is invalid.
            if (Lt->Kind == TypeKind::Set || Rt->Kind == TypeKind::Set) {
                if (Lt->Kind != TypeKind::Set || Rt->Kind != TypeKind::Set) {
                    error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                } else if (E.Op == TokenKind::LessThan
                        || E.Op == TokenKind::GreaterThan) {
                    error(E.Loc, diag::err_op_set_ordering, {opSpelling(E.Op)});
                } else {
                    unifyLooseSets(*E.Left, *E.Right, Lt, Rt);
                }
                return TyBool;
            }
            // EP §6.8.3.5 Table 4: complex supports only = and <> (not ordered).
            if (Lt->Kind == TypeKind::Complex || Rt->Kind == TypeKind::Complex) {
                if (E.Op != TokenKind::Equal && E.Op != TokenKind::NotEqual)
                    error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                return TyBool;
            }
            // ISO §6.7.2.5: pointers compare with = and <> only, and nil is
            // comparable with any pointer.  Without this a traversal cannot
            // even ask whether it has reached the end of a list.
            if (Lt->Kind == TypeKind::Pointer || Rt->Kind == TypeKind::Pointer
                || Lt->Kind == TypeKind::Nil  || Rt->Kind == TypeKind::Nil) {
                const bool BothPtrLike =
                    (Lt->Kind == TypeKind::Pointer || Lt->Kind == TypeKind::Nil)
                 && (Rt->Kind == TypeKind::Pointer || Rt->Kind == TypeKind::Nil);
                if (!BothPtrLike)
                    error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                else if (E.Op != TokenKind::Equal && E.Op != TokenKind::NotEqual)
                    error(E.Loc, diag::err_op_pointer_ordering, {opSpelling(E.Op)});
                else if (Lt->Kind == TypeKind::Pointer
                         && Rt->Kind == TypeKind::Pointer && Lt != Rt)
                    error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                return TyBool;
            }
            // ISO §6.7.2.5 lists the comparable types, and arrays, records and
            // files are not among them.  Matching kinds alone let these
            // through to codegen, which has no instruction for them.
            auto isUncomparable = [&](const Type& T) {
                if (isStringLike(T)) return false;
                return T.Kind == TypeKind::Array || T.Kind == TypeKind::Record
                    || T.Kind == TypeKind::File;
            };
            if (isUncomparable(*Lt) || isUncomparable(*Rt)) {
                error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                return TyBool;
            }
            // ISO §6.7.2.5: the operands of a relational operator have to be
            // compatible, which for ordinals is §6.4.5 — the same type, or one
            // a subrange of the other, or both subranges of the one host type.
            // Matching kinds were asked for instead, which is neither
            // sufficient nor necessary: it made two distinct enumerated types
            // comparable, and subranges of unrelated hosts with them, while
            // refusing a subrange against the very type it was cut from, since
            // Subrange and Enum are different kinds.  Only a subrange of
            // integer got through, and only because the numeric test below
            // looks past the subrange to its host.
            bool Ok = isAssignCompatible(*Lt, *Rt)
                   || isAssignCompatible(*Rt, *Lt)
                   || (Lt->isNumeric() && Rt->isNumeric())
                   || (isStringLike(*Lt) && isStringLike(*Rt));
            if (!Ok)
                error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
            else
                warnIfComparisonIsSettled(E, *Lt, *Rt);
            return TyBool;
        }

        case TokenKind::In: {
            if (!Lt->isOrdinal()) {
                error(E.Left->Loc, diag::err_in_lhs_not_ordinal, {Lt->Name});
            }
            if (Rt->Kind != TypeKind::Set) {
                error(E.Right->Loc, diag::err_in_rhs_not_set, {Rt->Name});
            } else if (isLooseSet(*E.Right) && Lt->isOrdinal() && !Lt->isError()
                       && Lt->Kind != TypeKind::Integer) {
                // `x in [...]` says nothing about the set's type but the type
                // of x, so that is the window the constructor is built in.
                // Without this, `x: -5..10; x in [-1, 3]` would test bit -1 of
                // a base-0 mask.  Plain integer is skipped deliberately: it
                // spans too much to be a window, so the constructor keeps the
                // one it derived from its own elements.
                adoptSetType(*E.Right, Ctx_.getSet(Lt, false));
            } else if (!Lt->isError() && Rt->ElemType && !Rt->ElemType->isError()) {
                // Check base type compatibility.
                if (!isAssignCompatible(*Rt->ElemType, *Lt))
                    error(E.Loc, diag::err_in_incompatible,
                          {Lt->Name, Rt->ElemType->Name});
            }
            return TyBool;
        }

        default:
            error(E.Loc, diag::err_unknown_binop);
            return TyErr;
    }
}

std::shared_ptr<Type> Sema::checkUnary(const UnaryExpr& E) {
    auto T = checkExpr(*E.Operand);
    if (T->isError()) return TyErr;
    if (T->isRestricted()) {
        error(E.Loc, diag::err_restricted_used, {T->Name});
        return TyErr;
    }

    switch (E.Op) {
        case TokenKind::Minus:
        case TokenKind::Plus:
            if (!T->isNumeric()) {
                error(E.Loc, diag::err_unary_numeric, {opSpelling(E.Op), T->Name});
                return TyErr;
            }
            return T;
        case TokenKind::Not:
            if (T->Kind != TypeKind::Boolean) {
                error(E.Loc, diag::err_not_requires_boolean, {T->Name});
                return TyErr;
            }
            return TyBool;
        default:
            error(E.Loc, diag::err_unknown_unop);
            return TyErr;
    }
}

bool Sema::checkBuiltinArity(const std::string& LowerName, SourceLocation Loc,
                             size_t NumArgs) {
    // ISO §6.6.6 and EP §6.7.6 fix the shape of each required function.  Only
    // the functions are listed: the required procedures are genuinely variadic
    // (write) or already checked where they are lowered.  A name absent from
    // the table is deliberately unconstrained.
    struct Arity { int Min, Max; };  // Max == -1 means no upper bound
    static const std::unordered_map<std::string, Arity> Table = {
        {"abs", {1,1}},   {"sqr", {1,1}},    {"sqrt", {1,1}},  {"sin", {1,1}},
        {"cos", {1,1}},   {"exp", {1,1}},    {"ln", {1,1}},    {"arctan", {1,1}},
        {"trunc", {1,1}}, {"round", {1,1}},  {"ord", {1,1}},   {"chr", {1,1}},
        {"odd", {1,1}},   {"length", {1,1}}, {"card", {1,1}},  {"trim", {1,1}},
        {"succ", {1,2}},  {"pred", {1,2}},           // EP §6.7.6.5 adds the count
        {"eof", {0,1}},   {"eoln", {0,1}},
        {"index", {2,2}}, {"substr", {2,3}},
        {"eq", {2,2}},    {"ne", {2,2}},     {"lt", {2,2}},    {"gt", {2,2}},
        {"le", {2,2}},    {"ge", {2,2}},
        {"cmplx", {2,2}}, {"polar", {2,2}},
        {"re", {1,1}},    {"im", {1,1}},     {"arg", {1,1}},
        {"position", {1,1}}, {"lastposition", {1,1}}, {"empty", {1,1}},
        {"date", {1,1}},  {"time", {1,1}},
    };
    const auto It = Table.find(LowerName);
    if (It == Table.end()) return true;
    const auto [Min, Max] = It->second;
    if ((int)NumArgs >= Min && (Max < 0 || (int)NumArgs <= Max)) return true;

    const auto Expected = (Max < 0)   ? std::to_string(Min) + " or more"
                        : (Min == Max) ? std::to_string(Min)
                                       : std::to_string(Min) + " or "
                                             + std::to_string(Max);
    const auto Got = std::to_string(NumArgs);
    error(Loc, diag::err_wrong_arg_count,
          {std::string_view(LowerName), std::string_view(Expected),
           std::string_view(Got)});
    return false;
}

bool Sema::checkEPOnly(const Symbol& Sym, SourceLocation Loc) {
    if (!Sym.IsEPOnly || Opts.extendedPascal()) return true;
    error(Loc, diag::err_ep_required_name, {Sym.Name});
    return false;
}

std::shared_ptr<Type> Sema::checkCallExpr(const CallExpr& E) {
    Symbol* Sym = Symtab.lookup(E.Name);
    if (!Sym) {
        error(E.Loc, diag::err_undefined_function, {E.Name});
        return TyErr;
    }
    if (Sym->Kind == SymbolKind::Builtin) {
        E.ResolvedBuiltin = true;
        std::string Lo = toLower(E.Name);
        if (!checkEPOnly(*Sym, E.Loc)) {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyErr;
        }
        if (!checkBuiltinArity(Lo, E.Loc, E.Args.size())) {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyErr;
        }
        // §6.6.6.5: eoln asks whether the position is at a line marker, and
        // only a text file has those.  eof applies to any file and is not
        // restricted here.
        if (Lo == "eoln" && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->Kind == TypeKind::File && ArgTy->ElemType
                && ArgTy->ElemType->Kind != TypeKind::Char)
                error(E.Args[0]->Loc, diag::err_line_proc_not_text,
                      {Lo, ArgTy->ElemType->Name});
            return TyBool;
        }
        // abs/sqr are polymorphic: return the argument's type.
        if ((Lo == "abs" || Lo == "sqr") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            // EP §6.7.6.2: abs(complex) → real; sqr(complex) → complex.
            if (ArgTy->Kind == TypeKind::Complex)
                return (Lo == "abs") ? TyReal : TyComplex;
            return (ArgTy->isNumeric() || ArgTy->isOrdinal()) ? ArgTy : TyErr;
        }
        // ISO §6.6.6.4: succ and pred stay in the argument's type, so
        // succ('a') is a char and succ(red) is the enumeration's next value.
        // EP §6.7.6.5 adds the two-argument form, which does not change this.
        if ((Lo == "succ" || Lo == "pred") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (E.Args.size() > 1 && !Opts.extendedPascal()) {
                error(E.Loc, diag::err_ep_two_arg_form, {Lo});
                return TyErr;
            }
            if (ArgTy->isError()) return TyErr;
            if (!ArgTy->isOrdinal()) {
                error(E.Loc, diag::err_ordinal_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return ArgTy;
        }
        // EP §6.7.6.7: substr/trim return the same string capacity as their input.
        if ((Lo == "substr" || Lo == "trim") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            return (ArgTy->Kind == TypeKind::VarString) ? ArgTy : TyStr;
        }
        // EP §6.7.6.2: math functions extended to complex — return complex when
        // the argument is complex, real otherwise.
        if (!E.Args.empty() && (Lo == "sqrt" || Lo == "sin" || Lo == "cos"
                || Lo == "exp" || Lo == "ln" || Lo == "arctan")) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (ArgTy->Kind == TypeKind::Complex) return TyComplex;
            return Sym->ReturnType ? Sym->ReturnType : TyErr; // TyReal
        }
        // EP §6.7.6.3: complex constructors.
        if (Lo == "cmplx" || Lo == "polar") {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyComplex;
        }
        // EP §6.7.6.2: component extraction functions.
        if (Lo == "re" || Lo == "im" || Lo == "arg") {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyReal;
        }
        // EP §6.7.6.8: binding names the variable whose binding it reports,
        // which must be one that could have been bound.
        if (Lo == "binding") {
            checkBindingCall(Lo, E.Loc, E.Args);
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
        return Sym->ReturnType ? Sym->ReturnType : TyErr;
    }
    return checkUserDefinedCall(*Sym, E.Loc, E.Args, /*expectFunction=*/true);
}

std::shared_ptr<Type> Sema::checkSetLit(const SetLiteralExpr& E) {
    if (E.Elements.empty()) {
        // Empty set: type is indeterminate; use a generic set-of-integer.
        auto T = std::make_shared<Type>();
        T->Kind     = TypeKind::Set;
        T->Name     = "[]";
        T->ElemType = TyInt;
        return T;
    }

    std::shared_ptr<Type> BaseType;
    for (const auto& Elem : E.Elements) {
        std::shared_ptr<Type> Et;
        if (auto* Rng = llvm::dyn_cast<SetRangeExpr>(Elem.get())) {
            auto Lo = checkExpr(*Rng->Low);
            auto Hi = checkExpr(*Rng->High);
            Et = Lo->isOrdinal() ? Lo : Hi;
        } else {
            Et = checkExpr(*Elem);
        }
        if (!Et->isError()) {
            if (!Et->isOrdinal()) {
                error(Elem->Loc, diag::err_set_elem_not_ordinal, {Et->Name});
            }
            if (!BaseType) BaseType = Et;
        }
    }

    auto T = std::make_shared<Type>();
    T->Kind     = TypeKind::Set;
    T->Name     = "set literal";
    T->ElemType = BaseType ? BaseType : TyInt;
    // With no context to name a set type, `card([-1, 3])` still has to put
    // ordinal -1 somewhere, so read a window off the elements themselves.  A
    // context that does name a type replaces this wholesale via adoptSetType.
    if (T->ElemType && T->ElemType->Kind == TypeKind::Integer) {
        if (auto Window = literalSetWindow(E))
            T->ElemType = Ctx_.getSubrange(TyInt, Window->first, Window->second);
    }
    return T;
}

/// The span of a set-constructor's ordinals, when every one of them folds and
/// the span reaches below zero.  Nothing otherwise: a window is only worth
/// deriving when the default of zero would be wrong.
std::optional<std::pair<int64_t, int64_t>>
Sema::literalSetWindow(const SetLiteralExpr& E) {
    int64_t Lo = 0, Hi = 0;
    bool Any = false;
    auto fold = [&](const ExprNode& X) {
        const auto V = constBound(X);
        if (!V) return false;
        if (!Any) { Lo = Hi = *V; Any = true; }
        else      { Lo = std::min(Lo, *V); Hi = std::max(Hi, *V); }
        return true;
    };
    for (const auto& Elem : E.Elements) {
        if (auto* Rng = llvm::dyn_cast<SetRangeExpr>(Elem.get())) {
            if (!fold(*Rng->Low) || !fold(*Rng->High)) return std::nullopt;
        } else if (!fold(*Elem)) {
            return std::nullopt;
        }
    }
    if (!Any || Lo >= 0) return std::nullopt;
    return std::make_pair(Lo, Hi);
}

// ---------------------------------------------------------------------------
// ISO §6.7.1: set-constructors take their type from the context
// ---------------------------------------------------------------------------

bool Sema::isLooseSet(const ExprNode& E) {
    if (!E.ResolvedType || E.ResolvedType->Kind != TypeKind::Set) return false;
    if (llvm::isa<SetLiteralExpr>(&E)) return true;
    // A set operator over loose operands is itself loose: in `[1] + [2]` there
    // is still nothing but the context to say which set type is meant.
    if (auto* B = llvm::dyn_cast<BinaryExpr>(&E))
        return isLooseSet(*B->Left) && isLooseSet(*B->Right);
    return false;
}

void Sema::adoptSetType(const ExprNode& E, const std::shared_ptr<Type>& Want) {
    if (!Want || Want->Kind != TypeKind::Set || !isLooseSet(E)) return;
    E.ResolvedType = Want;
    if (auto* B = llvm::dyn_cast<BinaryExpr>(&E)) {
        adoptSetType(*B->Left,  Want);
        adoptSetType(*B->Right, Want);
    }
}

void Sema::unifyLooseSets(const ExprNode& L, const ExprNode& R,
                          const std::shared_ptr<Type>& Lt,
                          const std::shared_ptr<Type>& Rt) {
    if (isLooseSet(L) == isLooseSet(R)) return; // nothing definite to copy
    if (isLooseSet(L)) adoptSetType(L, Rt);
    else               adoptSetType(R, Lt);
}

// ---------------------------------------------------------------------------
// EP §6.8.7: Structured value constructor checking
// ---------------------------------------------------------------------------

std::shared_ptr<Type> Sema::checkStructuredValue(const StructuredValueExpr& E) {
    // Helper: check all sub-expressions regardless of errors (avoids cascades).
    auto checkAllArms = [&]() {
        for (const auto& arm : E.Arms) {
            for (const auto& lbl : arm.Labels) (void)checkExpr(*lbl);
            if (arm.Value) (void)checkExpr(*arm.Value);
        }
    };

    // EP §6.8.7.1: a component-value is written without a type name, the type
    // being the one the place it appears in calls for.  Sema hands that type
    // in through ExpectedValueType_ rather than looking one up.
    auto T = ExpectedValueType_;
    if (!E.TypeName.empty() || !T) {
        // Look up the type name as a TypeAlias in the symbol table.
        Symbol* Sym = Symtab.lookup(E.TypeName);
        if (!Sym || Sym->Kind != SymbolKind::TypeAlias || !Sym->Ty
                 || Sym->Ty->isError()) {
            error(E.Loc, diag::err_constructor_type_not_found, {E.TypeName});
            checkAllArms();
            return TyErr;
        }
        T = Sym->Ty;
    }
    // What is expected of an arm is settled by the arm, not by the value it
    // belongs to, so it does not carry on down.
    const auto SavedExpected = ExpectedValueType_;
    ExpectedValueType_       = nullptr;
    struct Restore {
        std::shared_ptr<Type>& Slot; std::shared_ptr<Type> Old;
        ~Restore() { Slot = Old; }
    } RestoreExpected{ExpectedValueType_, SavedExpected};

    if (T->Kind == TypeKind::Array) {
        auto elemTy = T->ElemType;
        for (const auto& arm : E.Arms) {
            if (!arm.IsOtherwise) {
                for (const auto& lbl : arm.Labels) (void)checkExpr(*lbl);
            }
            if (arm.Value) {
                // A component-value of an element is written bare too, and
                // the element type is what says which type it has.
                ExpectedValueType_ = elemTy;
                auto valTy = checkExpr(*arm.Value);
                ExpectedValueType_ = nullptr;
                if (elemTy && !elemTy->isError() && !valTy->isError()
                    && !isAssignCompatible(*elemTy, *valTy))
                    error(arm.Value->Loc, diag::err_assign_mismatch,
                          {valTy->Name, elemTy->Name});
                adoptSetType(*arm.Value, elemTy);
            }
        }
        return T;
    }

    if (T->Kind == TypeKind::Record) {
        for (const auto& arm : E.Arms) {
            const Type::Field* Named = nullptr;
            for (const auto& lbl : arm.Labels) {
                if (auto* id = llvm::dyn_cast<IdentExpr>(lbl.get())) {
                    const Type::Field* F = T->fieldByName(id->Name);
                    if (!F)
                        error(lbl->Loc, diag::err_record_constructor_unknown_field,
                              {id->Name, T->Name});
                    else if (!Named)
                        Named = F;
                } else {
                    // Non-identifier labels not valid in record constructors
                    (void)checkExpr(*lbl);
                }
            }
            if (arm.Value) {
                if (Named) ExpectedValueType_ = Named->Ty;
                (void)checkExpr(*arm.Value);
                ExpectedValueType_ = nullptr;
                if (Named) adoptSetType(*arm.Value, Named->Ty);
            }
        }
        return T;
    }

    if (T->Kind == TypeKind::Set) {
        // Typed set constructor — treat each arm's labels as set elements.
        for (const auto& arm : E.Arms) {
            for (const auto& lbl : arm.Labels) (void)checkExpr(*lbl);
            if (arm.Value) (void)checkExpr(*arm.Value);
        }
        return T;
    }

    error(E.Loc, diag::err_constructor_not_aggregate, {T->Name});
    checkAllArms();
    return TyErr;
}

// ---------------------------------------------------------------------------
// Call argument checking
// ---------------------------------------------------------------------------

bool Sema::typeContainsFile(const Type& T) {
    switch (T.Kind) {
    case TypeKind::File:   return true;
    case TypeKind::Record:
        for (const auto& F : T.RecordFields)
            if (F.Ty && typeContainsFile(*F.Ty)) return true;
        return false;
    // An array of files holds files as surely as a record of them does, and
    // was the way round the rule.
    case TypeKind::Array:
    case TypeKind::Set:
        return T.ElemType && typeContainsFile(*T.ElemType);
    default:               return false;
    }
}

namespace {

// EP §6.4.7: Returns true if two SchemaInstance types represent the same
// instantiation (same schema name and identical discriminant values).
bool schemaInstMatch(const Type& A, const Type& B) {
    if (A.Kind != TypeKind::SchemaInstance || B.Kind != TypeKind::SchemaInstance)
        return false;
    if (A.SchemaName != B.SchemaName) return false;
    if (A.SchemaDiscs.size() != B.SchemaDiscs.size()) return false;
    for (size_t I = 0; I < A.SchemaDiscs.size(); ++I)
        if (A.SchemaDiscs[I].Value != B.SchemaDiscs[I].Value) return false;
    return true;
}
} // anonymous namespace

std::shared_ptr<Type>
Sema::checkUserDefinedCall(const Symbol& Sym, SourceLocation CallLoc,
                           std::span<const std::unique_ptr<ExprNode>> Args,
                           bool ExpectFunction) {
    if (Sym.Kind != SymbolKind::Proc) {
        error(CallLoc, diag::err_not_callable, {Sym.Name});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }
    if (ExpectFunction && !Sym.IsFunction) {
        error(CallLoc, diag::err_proc_cannot_return_value, {Sym.Name});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }
    if (!ExpectFunction && Sym.IsFunction) {
        error(CallLoc, diag::err_func_as_statement, {Sym.Name});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }
    checkCallArgs(Sym, CallLoc, Args);
    return Sym.ReturnType ? Sym.ReturnType : TyErr;
}

/// EP §6.7.3.7: are two conformant array schemas the same one?
///
/// The bound variable names are local to each heading, exactly as parameter
/// names are, so only the index ordinal types and the element type count.
bool Sema::congruousConformant(const Type& A, const Type& B) const {
    if (A.ConformantBounds.size() != B.ConformantBounds.size()) return false;
    for (size_t I = 0; I < A.ConformantBounds.size(); ++I)
        if (!isIdenticalType(A.ConformantBounds[I].OrdType,
                             B.ConformantBounds[I].OrdType))
            return false;
    if (!A.ElemType || !B.ElemType) return false;
    if (A.ElemType->Kind != B.ElemType->Kind) return false;
    if (A.ElemType->Kind == TypeKind::ConformantArray)
        return congruousConformant(*A.ElemType, *B.ElemType);
    return isIdenticalType(A.ElemType, B.ElemType);
}

/// Do two formal parameters, written in two different headings, denote the
/// same type?
///
/// Identity is the rule, and for most types pointer identity answers it
/// because TypeContext hands out one instance per distinct type.  Three of the
/// parameter forms are not interned, though: a procedural parameter, a
/// conformant array and an undiscriminated schema are each written out afresh
/// wherever they appear, so identity would call two equal types different and
/// produce the memorable complaint that a type does not match itself.  Those
/// are compared structurally.
///
/// This is what both ISO §6.6.3.6 congruity and the forward-declaration check
/// need, and the two disagreeing is how the conformant-array case stayed
/// broken.
bool Sema::sameParamType(const std::shared_ptr<Type>& A,
                         const std::shared_ptr<Type>& B) const {
    if (!A || !B) return false;
    if (A->Kind != B->Kind) return false;
    if (isCallable(*A))                       return congruousSignature(*A, *B);
    if (A->Kind == TypeKind::ConformantArray)  return congruousConformant(*A, *B);
    // EP §6.7.3.2: an undiscriminated schema parameter is fixed by naming its
    // schema; the discriminants arrive with the actual either way.
    if (A->Kind == TypeKind::Schema)
        return toLower(A->SchemaName) == toLower(B->SchemaName);
    return isIdenticalType(A, B);
}

/// ISO §6.6.3.6: are two parameter lists congruous?
///
/// Congruity is deliberately strict.  The lists must have the same length;
/// each pair must agree on being a var parameter, since one passes an address
/// and the other a copy; and their types must be the same rather than merely
/// compatible, because the callee will be reached through a signature it was
/// not compiled against.  Parameter names are not compared — they are local to
/// each heading.  A parameter that is itself procedural recurses.
bool Sema::congruousSignature(const Type& A, const Type& B) const {
    if (A.Kind != B.Kind) return false;              // procedure vs function
    if (A.Params.size() != B.Params.size()) return false;
    for (size_t I = 0; I < A.Params.size(); ++I) {
        if (A.Params[I].IsVar != B.Params[I].IsVar) return false;
        if (!sameParamType(A.Params[I].Ty, B.Params[I].Ty)) return false;
    }
    if (static_cast<bool>(A.RetType) != static_cast<bool>(B.RetType)) return false;
    return !A.RetType || isIdenticalType(A.RetType, B.RetType);
}

/// Checks the actual supplied for a procedural or functional parameter.
void Sema::checkProcedureActual(const Type& Formal, const std::string& ParamName,
                                const ExprNode& Arg) {
    const auto* Id = llvm::dyn_cast<IdentExpr>(&Arg);
    if (!Id) {
        error(Arg.Loc, diag::err_proc_param_needs_proc_name, {ParamName});
        return;
    }
    const Symbol* S = Symtab.lookup(Id->Name);
    if (!S) {
        error(Arg.Loc, diag::err_undefined_procedure, {Id->Name});
        return;
    }
    // ISO §6.6.3.1: a required procedure or function is not an ordinary one —
    // write, read and friends are variadic or polymorphic and have no single
    // heading to be congruous with.
    if (S->Kind == SymbolKind::Builtin) {
        error(Arg.Loc, diag::err_proc_param_is_required, {Id->Name});
        return;
    }
    if (S->Kind != SymbolKind::Proc) {
        error(Arg.Loc, diag::err_proc_param_needs_proc_name, {ParamName});
        return;
    }

    Type Actual;
    Actual.Kind    = S->IsFunction ? TypeKind::Function : TypeKind::Procedure;
    Actual.Params  = S->Params;
    Actual.RetType = S->ReturnType;

    if (!congruousSignature(Formal, Actual)) {
        const std::string Want = describeCallable(Formal);
        const std::string Got  = describeCallable(Actual);
        error(Arg.Loc, diag::err_proc_param_not_congruous,
              {Id->Name, Got, ParamName, Want});
    }
}

void Sema::checkCallArgs(const Symbol& Sym, SourceLocation CallLoc,
                         std::span<const std::unique_ptr<ExprNode>> Args) {
    if (Args.size() != Sym.Params.size()) {
        auto ExpStr = std::to_string(Sym.Params.size());
        auto GotStr = std::to_string(Args.size());
        error(CallLoc, diag::err_wrong_arg_count,
              {std::string_view(Sym.Name), std::string_view(ExpStr),
               std::string_view(GotStr)});
        // Still type-check the arguments we have.
        for (const auto& A : Args) (void)checkExpr(*A);
        return;
    }
    for (auto&& [Idx, Param, ArgPtr] : std::views::zip(std::views::iota(size_t{0}), Sym.Params, Args)) {
        const auto& ArgNode = *ArgPtr;

        // ISO §6.6.3.1: the actual for a procedural parameter is the name of a
        // procedure, which is not an expression — checking it as one would
        // report it as an undefined variable or try to call it with no
        // arguments.  It is matched against the formal before that can happen.
        if (Param.Ty && isCallable(*Param.Ty)) {
            checkProcedureActual(*Param.Ty, Param.Name, ArgNode);
            continue;
        }

        auto At = checkExpr(ArgNode);

        // EP §6.7.3.2/§6.7.3.3: an undiscriminated schema parameter accepts any
        // instance of the same schema, in the var and value cases alike; the
        // discriminants travel with the actual parameter.
        if (Param.Ty && Param.Ty->Kind == TypeKind::Schema) {
            if (!isAssignCompatible(*Param.Ty, *At))
                error(ArgNode.Loc, diag::err_schema_arg_not_schematic,
                      {Param.Name, Param.Ty->Name, At->Name});
            if (Param.IsVar && !isLValue(ArgNode)) {
                auto IdxStr = std::to_string(Idx + 1);
                error(ArgNode.Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view(IdxStr), std::string_view(Sym.Name)});
            }
            continue;
        }

        // ISO §6.6.3.8: a conformant array parameter takes any array that
        // conforms to its schema, whatever bounds that array was declared with.
        if (Param.Ty && Param.Ty->Kind == TypeKind::ConformantArray) {
            if (!isConformable(*Param.Ty, *At)) {
                if (At->Kind != TypeKind::Array && At->Kind != TypeKind::ConformantArray) {
                    error(ArgNode.Loc, diag::err_conformant_actual_not_array,
                          {Param.Name, At->Name});
                } else {
                    error(ArgNode.Loc, diag::err_conformant_elem_mismatch,
                          {Param.Name,
                           Param.Ty->ElemType ? Param.Ty->ElemType->Name : "?",
                           At->ElemType       ? At->ElemType->Name       : "?"});
                }
            }
            // Conformant var params still require an lvalue.
            if (Param.IsVar && !isLValue(ArgNode)) {
                auto IdxStr = std::to_string(Idx + 1);
                error(ArgNode.Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view(IdxStr), std::string_view(Sym.Name)});
            }
        } else if (Param.IsVar) {
            if (!isLValue(ArgNode)) {
                auto IdxStr = std::to_string(Idx + 1);
                error(ArgNode.Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view(IdxStr), std::string_view(Sym.Name)});
            }
            // ISO §6.6.3.3: for var parameters the types must be *identical*
            // (same canonical type instance via TypeContext).
            // EP §6.4.7: schema instances are identical when name+discriminants match.
            // EP §6.7.3.3: with a restricted type in play the two need not be
            // the same type — one may be the underlying type of the other, so
            // that a variable of the restricted type can be passed to a
            // formal of the type it restricts, and the other way about.
            bool typeOk = isIdenticalType(At, Param.Ty)
                       || (At && Param.Ty && schemaInstMatch(*At, *Param.Ty))
                       || (At && At->isRestricted()
                           && isIdenticalType(At->RestrictedOf, Param.Ty))
                       || (Param.Ty && Param.Ty->isRestricted()
                           && isIdenticalType(At, Param.Ty->RestrictedOf));
            if (!typeOk) {
                if (At && Param.Ty && At->Name == Param.Ty->Name)
                    error(ArgNode.Loc, diag::err_var_param_distinct_anon_type,
                          {Param.Name, Param.Ty->Name});
                else
                    error(ArgNode.Loc, diag::err_var_param_type_mismatch,
                          {Param.Name, Param.Ty->Name, At->Name});
            }
            // ISO §6.6.3.3: additional restrictions on actual var parameters.
            if (auto* Fe = llvm::dyn_cast<FieldExpr>(&ArgNode)) {
                auto RecTy = checkExpr(*Fe->Record);
                if (!RecTy->isError()) {
                    const Type::Field* F = RecTy->fieldByName(Fe->Field);
                    if (F && F->IsTagField)
                        error(ArgNode.Loc, diag::err_tag_field_var_param, {Fe->Field});
                    if (RecTy->Packed)
                        error(ArgNode.Loc, diag::err_packed_struct_var_param);
                }
            } else if (auto* Ie = llvm::dyn_cast<IndexExpr>(&ArgNode)) {
                auto ArrTy = checkExpr(*Ie->Array);
                if (!ArrTy->isError() && ArrTy->Packed)
                    error(ArgNode.Loc, diag::err_packed_array_var_param);
            } else if (auto* Id = llvm::dyn_cast<IdentExpr>(&ArgNode)) {
                Symbol* Vsym = Symtab.lookup(Id->Name);
                if (Vsym && Vsym->FromPackedWith)
                    error(ArgNode.Loc, diag::err_packed_struct_var_param);
            }
        } else {
            // EP §6.7.3.2: it is the value in the *underlying* type of the
            // actual parameter that must suit the formal, which is how a
            // restricted value reaches a formal of the type it restricts —
            // the one use §6.4.2.5 leaves open for it.
            if (At->isRestricted()) At = At->RestrictedOf;
            if (!At->isError() && !Param.Ty->isError()
                && !isAssignCompatible(*Param.Ty, *At))
                error(ArgNode.Loc, diag::err_param_type_mismatch,
                      {Param.Name, Param.Ty->Name, At->Name});
            // ISO §6.6.3.2: value parameters may not have a type that contains a file.
            if (!Param.Ty->isError() && typeContainsFile(*Param.Ty))
                error(ArgNode.Loc, diag::err_file_value_param, {Param.Name});
            adoptSetType(ArgNode, Param.Ty);
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool Sema::isLValue(const ExprNode& E) const {
    if (auto* Id = llvm::dyn_cast<IdentExpr>(&E)) {
        // ISO §6.8.2.2: the name of a function whose block contains this
        // statement stands for that activation's result, and is assignable.
        // Any function up the chain will do, the clause asking that the block
        // contain the assignment rather than be the one it is written in.
        if (resultFrameFor(Id->Name)) return true;
        const Symbol* Sym = Symtab.lookup(Id->Name);
        if (!Sym) return false;
        return Sym->Kind == SymbolKind::Var || Sym->Kind == SymbolKind::VarParam;
    }
    if (auto* Ix  = llvm::dyn_cast<IndexExpr>(&E))  return isLValue(*Ix->Array);
    if (auto* Fld = llvm::dyn_cast<FieldExpr>(&E))  return isLValue(*Fld->Record);
    if (llvm::dyn_cast<DerefExpr>(&E))               return true;
    // EP §6.5.6: a substring-variable is a variable, so a substring of a
    // variable may be assigned to.
    if (auto* Sub = llvm::dyn_cast<SubstringExpr>(&E)) return isLValue(*Sub->Str);
    return false;
}

bool Sema::isConformable(const Type& Formal, const Type& Actual) const {
    if (Formal.Kind != TypeKind::ConformantArray) return false;
    if (Actual.Kind != TypeKind::Array
            && Actual.Kind != TypeKind::ConformantArray)
        return false;
    if (!Formal.ElemType || !Actual.ElemType) return true;
    // ISO §6.6.3.8: the element type of the actual conforms in its turn when
    // the formal's is another schema, which is how a parameter of more than
    // one dimension is written.
    if (Formal.ElemType->Kind == TypeKind::ConformantArray)
        return isConformable(*Formal.ElemType, *Actual.ElemType);
    return isAssignCompatible(*Formal.ElemType, *Actual.ElemType);
}

bool Sema::isAssignCompatible(const Type& Dst, const Type& Src,
                              bool ExactBounds) const {
    if (Dst.isError() || Src.isError())   return true;  // suppress cascades

    // EP §6.4.6 a): two types that are the same are assignment-compatible only
    // if the type may be the component-type of a file, which §6.4.3.6 says a
    // restricted type may not be.  So no value is assignment-compatible with a
    // restricted type, and a value of one is compatible with nothing: the
    // assignments it takes part in are the ones §6.9.2.2 and §6.7.3 name.
    if (Dst.isRestricted() || Src.isRestricted()) return false;

    // EP §6.4.7: SchemaInstance — compatible if same schema+discriminant values,
    // or fall through to body-type compatibility.
    if (Dst.Kind == TypeKind::SchemaInstance && Src.Kind == TypeKind::SchemaInstance) {
        if (Dst.SchemaName == Src.SchemaName
                && Dst.SchemaDiscs.size() == Src.SchemaDiscs.size()) {
            bool same = true;
            for (size_t I = 0; I < Dst.SchemaDiscs.size(); ++I)
                if (Dst.SchemaDiscs[I].Value != Src.SchemaDiscs[I].Value) { same = false; break; }
            if (same) return true;
        }
        if (Dst.SchemaBody && Src.SchemaBody)
            return isAssignCompatible(*Dst.SchemaBody, *Src.SchemaBody);
        return false;
    }
    // EP §6.7.3.2/§6.7.3.3: an undiscriminated schema accepts any instance of
    // the same schema; the discriminants travel with the value.
    if (Dst.Kind == TypeKind::Schema || Src.Kind == TypeKind::Schema) {
        const Type& Sch   = Dst.Kind == TypeKind::Schema ? Dst : Src;
        const Type& Other = Dst.Kind == TypeKind::Schema ? Src : Dst;
        if (Other.Kind == TypeKind::Schema || Other.Kind == TypeKind::SchemaInstance)
            return eqCI(Sch.SchemaName, Other.SchemaName);
        // §6.7.3.3 requires both sides to be schematic; a plain array is not.
        return false;
    }
    if (Dst.Kind == TypeKind::SchemaInstance && Dst.SchemaBody)
        return isAssignCompatible(*Dst.SchemaBody, Src);
    if (Src.Kind == TypeKind::SchemaInstance && Src.SchemaBody)
        return isAssignCompatible(Dst, *Src.SchemaBody);

    // A conformant array parameter is a type of its own and no array is the
    // same one, so nothing is assignable to it whole.  What an array may do is
    // conform to it, which is a rule about passing a parameter and not about
    // assigning a value; see isConformable.  The two were one rule here, and
    // assignment took the parameter's half of it: `x := a` inside the body
    // copied a's length into whatever the caller had passed, so an array of
    // eight assigned through a parameter bound to an array of three wrote five
    // elements past the end of it.
    if (Dst.Kind == TypeKind::ConformantArray) return false;
    if (Dst.Kind == Src.Kind) {
        switch (Dst.Kind) {
            // Scalar built-in types: kind equality suffices.
            case TypeKind::Integer: case TypeKind::Real: case TypeKind::Boolean:
            case TypeKind::Char:    case TypeKind::String: case TypeKind::Nil:
            case TypeKind::VarString:
                return true;

            // ISO §6.4.2.3: each enumerated-type definition is a distinct
            // type, so two of them agree only when they are the same
            // declaration — by name, or by identity when written inline.
            case TypeKind::Enum:
                if (isAnonymousNominal(Dst) || isAnonymousNominal(Src))
                    return &Dst == &Src;
                return Dst.Name == Src.Name;

            // ISO §6.4.5 b): two subranges are compatible when they are
            // subranges of the one host type, whatever bounds each was written
            // with.  Whether the value lands inside the destination's interval
            // is §6.4.6 c), a condition on the value rather than on the type,
            // and codegen emits the check that reports it.
            //
            // Bounds were required to agree here, which made `a := b` between
            // two subranges of integer a type error, and `set of 1..10 :=
            // set of 1..100` another — both ordinary Pascal.  They still have to
            // agree where the question is whether two types are the same one,
            // since there the bounds decide the size and the layout.
            case TypeKind::Subrange:
                if (ExactBounds
                        && (Dst.SubLo != Src.SubLo || Dst.SubHi != Src.SubHi))
                    return false;
                if (Dst.SubBase && Src.SubBase)
                    return isAssignCompatible(*Dst.SubBase, *Src.SubBase,
                                              ExactBounds);
                return true;

            // Structural type compatibility (ISO §6.4.5).
            // Two array types are compatible when their element types are compatible
            // and their index types are compatible.
            case TypeKind::Array:
                if (!Dst.ElemType || !Src.ElemType)  return false;
                if (!isAssignCompatible(*Dst.ElemType, *Src.ElemType,
                                        /*ExactBounds=*/true)) return false;
                if (Dst.IndexType && Src.IndexType)
                    return isAssignCompatible(*Dst.IndexType, *Src.IndexType,
                                              /*ExactBounds=*/true);
                return true;

            // Record: a declared record type is identified by its declaration
            // (ISO §6.4.3.3), so two of them agree only when they are the same
            // declaration.  A record written inline has no declared name; those
            // are compared structurally, which is what lets an inline
            // parameter type accept an inline variable of the same shape.
            case TypeKind::Record:
                if (!isAnonymousNominal(Dst) || !isAnonymousNominal(Src))
                    return Dst.Name == Src.Name;
                if (Dst.RecordFields.size() != Src.RecordFields.size()) return false;
                for (size_t I = 0; I < Dst.RecordFields.size(); ++I) {
                    if (!Dst.RecordFields[I].Ty || !Src.RecordFields[I].Ty) return false;
                    if (!isAssignCompatible(*Dst.RecordFields[I].Ty,
                                            *Src.RecordFields[I].Ty,
                                            /*ExactBounds=*/true)) return false;
                }
                return true;

            // ISO §6.4.5 c): two set-types are compatible when their base-types
            // are, so the bounds of the base are not asked to agree.
            case TypeKind::Set:
                if (Src.Name == "set literal" || Src.Name == "[]") return true;
                if (!Dst.ElemType || !Src.ElemType) return Dst.Name == Src.Name;
                return isAssignCompatible(*Dst.ElemType, *Src.ElemType);

            // Pointer: compatible when pointee types are compatible.
            case TypeKind::Pointer:
                if (!Dst.PointeeType || !Src.PointeeType) return false;
                return isAssignCompatible(*Dst.PointeeType, *Src.PointeeType,
                                          /*ExactBounds=*/true);

            default:
                return Dst.Name == Src.Name;
        }
    }
    if (Dst.Kind == TypeKind::Real    && Src.Kind == TypeKind::Integer) return true;
    // EP §6.4.2.2: integer → real → complex widening chain.
    if (Dst.Kind == TypeKind::Complex && Src.Kind == TypeKind::Real)    return true;
    if (Dst.Kind == TypeKind::Complex && Src.Kind == TypeKind::Integer) return true;
    if (Dst.Kind == TypeKind::Complex && Src.Kind == TypeKind::Complex) return true;
    if (Dst.Kind == TypeKind::String  && Src.Kind == TypeKind::Char)    return true;
    if (Dst.Kind == TypeKind::Pointer && Src.Kind == TypeKind::Nil)     return true;
    // A set literal is compatible with any set destination type.
    if (Dst.Kind == TypeKind::Set && Src.Kind == TypeKind::Set
        && (Src.Name == "set literal" || Src.Name == "[]")) return true;
    // Subrange ↔ base type.
    if (Dst.Kind == TypeKind::Subrange && Dst.SubBase)
        return isAssignCompatible(*Dst.SubBase, Src, ExactBounds);
    if (Src.Kind == TypeKind::Subrange && Src.SubBase)
        return isAssignCompatible(Dst, *Src.SubBase, ExactBounds);

    // EP §6.4.5–6.4.6: string compatibility rules.
    // VarString(M) ← VarString(N): always allowed (truncation at runtime if N>M)
    if (Dst.Kind == TypeKind::VarString && Src.Kind == TypeKind::VarString) return true;
    // VarString(N) ← char
    if (Dst.Kind == TypeKind::VarString && Src.Kind == TypeKind::Char)   return true;
    // VarString(N) ← plain string literal / String
    if (Dst.Kind == TypeKind::VarString && Src.Kind == TypeKind::String)  return true;
    // String (7185) ← VarString: allow for passing to legacy write/writeln
    if (Dst.Kind == TypeKind::String && Src.Kind == TypeKind::VarString)  return true;

    // ISO §6.4.3.2: a string-type takes a string value of exactly its own
    // length.  A literal arrives as String or, in EP, as VarString carrying
    // its length; checkStringCapacity is what holds it to the exact length,
    // since a String does not record one.
    // Two string-types are two arrays, and were already settled above.
    if (isCharStringType(Dst)) {
        if (Src.Kind == TypeKind::String) return true;
        if (Src.Kind == TypeKind::VarString)
            return Src.StrCapacity == charStringLength(Dst);
    }
    // A string-type is a string value, so it goes where one is expected.
    if (Dst.Kind == TypeKind::VarString && isCharStringType(Src))
        return true;
    return false;
}

std::shared_ptr<Type> Sema::numericResult(const Type& L, const Type& R) const {
    if (L.Kind == TypeKind::Complex || R.Kind == TypeKind::Complex) return TyComplex;
    if (L.Kind == TypeKind::Real    || R.Kind == TypeKind::Real)    return TyReal;
    return TyInt;
}
