#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <span>

using namespace plang;

// See NumExprKinds in AstBase.h.
static_assert(NumExprKinds == 18, "a new expression needs a case in checkExpr");

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

// Ceiling on live checkExpr activations (Sema::ExprDepth).  Unlike deeply
// NESTED parenthesized input, which Parser::ExprDepth (see ParseExpr.cpp)
// already bounds, a flat operator chain like `1+1+1+...+1` is parsed
// ITERATIVELY by precedence climbing, so its AST can be arbitrarily deep with
// no parser-side ceiling on it; checkExpr's own recursive walk of that AST is
// the first place this needs a guard.  MaxExprDepth/ExprDepth/
// ExprDepthLimitHit/ExprDepthScope are declared on Sema (Sema.h) rather than
// here, now that constBound/buildExtentForm (SemaType.cpp) share them too --
// see the comment there.

std::shared_ptr<Type> Sema::checkExpr(const ExprNode& E) {
    // See MaxExprDepth (Sema.h). Checked before the RAII bump: a caller
    // already sitting at the ceiling must return without recursing again,
    // not recurse once more and only then stop.
    if (ExprDepth >= MaxExprDepth) {
        if (!ExprDepthLimitHit) {
            ExprDepthLimitHit = true;
            error(E.Loc, diag::err_expr_too_deeply_nested);
        }
        E.ResolvedType = TyErr;
        return TyErr;
    }
    ExprDepthScope DepthGuard(ExprDepth, ExprDepthLimitHit);

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
            // Turbo has no VarString at all (isVarStringLike is false for
            // EVERY Turbo type -- ShortString is a structurally distinct
            // Kind), so unconditionally typing '' as EP's VarString(0) left
            // it the one string-shaped value under -std=turbo that every
            // ShortString operator (assignment, +, comparison -- each keyed
            // on isShortStringLike(T)/T->Kind==String) refused to touch:
            // `s := ''` for a ShortString s failed to even type-check
            // ("cannot assign 'string(0)' to variable of type 'string[10]'"),
            // and `s = ''`/`s <> ''` failed the same way.  This follows the
            // same EP-vs-not split every OTHER string literal on this arm
            // already makes just below (TyStr is what a multi-character
            // literal already resolves to outside EP, and every ShortString
            // operator already accepts a TyStr operand for exactly that
            // literal case) rather than treating the empty case as a special
            // one-off.
            T = Opts.extendedPascal() ? Type::makeVarString(0) : TyStr;
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
    else if (auto* N = llvm::dyn_cast<MethodCallExpr>(&E)) T = checkMethodCallExpr(*N);
    else if (auto* N = llvm::dyn_cast<SetLiteralExpr>(&E)) T = checkSetLit(*N);
    else if (auto* N = llvm::dyn_cast<StructuredValueExpr>(&E)) T = checkStructuredValue(*N);
    else if (auto* N = llvm::dyn_cast<TypeCastExpr>(&E))   T = checkTypeCast(*N);
    else if (auto* N = llvm::dyn_cast<WriteParam>(&E)) {
        T = checkExpr(*N->Value);
        // ISO §6.9.3.1 / EP §6.10.3.1: TotalWidth and FracDigits are
        // integer-expressions.  Left unchecked, a char or real here reached
        // CodeGen's toI64, which has no diagnostic of its own: a char widens
        // by its ordinal value (write(x:'a') asks for a field 97 wide) and a
        // real truncates silently (write(x:2.5) asks for a field 2 wide).
        if (N->Width) {
            auto WidthTy = checkExpr(*N->Width);
            if (!WidthTy->isError() && !WidthTy->isIntegral())
                error(N->Width->Loc, diag::err_write_field_not_integer,
                      {"width", WidthTy->Name});
        }
        if (N->Decimals) {
            auto DecimalsTy = checkExpr(*N->Decimals);
            if (!DecimalsTy->isError() && !DecimalsTy->isIntegral())
                error(N->Decimals->Loc, diag::err_write_field_not_integer,
                      {"decimals", DecimalsTy->Name});
        }
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
        if (!isVarStringLike(T.get()))
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
    // Recorded before anything else decides what the name means: what codegen
    // needs to know is whether the PROGRAM declared it, and that is a fact
    // about this scope rather than about which of codegen's tables happens to
    // hold the spelling.
    // Not merely "found": ISO §6.2.2.10 puts the required identifiers in a
    // region ENCLOSING the program, and Sema models that by defining each one
    // as a Builtin symbol -- so a plain lookup finds `eof` in every program
    // ever written.  What matters here is a declaration nearer than that one.
    if (const Symbol* S = Symtab.lookup(E.Name);
            S && S->Kind != SymbolKind::Builtin) {
        E.UserDeclared = true;
        if (S->Kind == SymbolKind::Proc)
            E.UserDeclaredCallable = true;
    }

    // Inside a function body, the function's own name (or named result variable,
    // EP §6.7.2) is the result pseudo-variable.
    // §6.8.2.2 says the function block must *contain* the assignment, not be
    // it, so a procedure nested inside the function names the result too.
    // Asking whether the innermost procedure happens to be that function got
    // the answer from the wrong place: resultFrameFor searches the stack of
    // functions whose blocks are open, innermost first, and already knows that
    // a nearer declaration of the name denotes something else.  It is what
    // isFunctionResultTarget uses to accept the assignment; this is the same
    // question asked one step later, for the type.
    //
    // ISO §6.8.2.2's own grammar only ever admits a bare function-identifier
    // in ONE place: the assignment-statement production, "(variable-access |
    // function-identifier) ':=' expression".  Nowhere else -- not this same
    // statement's own RHS, not an argument, not a plain read three lines
    // later -- does a bare function-identifier mean the result; everywhere
    // else it is §6.7.3's function-designator, i.e. a (possibly recursive)
    // call.  This project's own history read it as the result EVERYWHERE
    // inside the function, target or not, which is Turbo Pascal's actual,
    // simpler-but-not-ISO-correct behaviour -- kept exactly as before for
    // ISO 7185/Extended Pascal (CurAssignTargetRoot_ stays unconsulted:
    // !Opts.turbo() alone decides it), restored to the ISO reading under
    // -std=turbo only, per this task's own scoping.  CurAssignTargetRoot_
    // is checkAssign's own bookkeeping (pointer identity, not a name
    // match): see its declaration in Sema.h.
    if (!FuncStack.empty()) {
        if (const FuncFrame* F = resultFrameFor(E.Name)) {
            if (!Opts.turbo() || &E == CurAssignTargetRoot_) {
                E.Resolution = IdentExpr::IdentResolution::ResultVariable;
                return F->RetType ? F->RetType : TyErr;
            }
            // Recursive call: fall through to the ordinary Symbol-based
            // resolution below, which already treats a bare (zero actual
            // parameters) required-or-declared function name as a call --
            // exactly what is needed to call this same function again, with
            // no special-casing of its own.  (If Fib takes parameters, that
            // same generic path reports err_function_requires_args: a bare
            // 'Fib' with no argument list cannot supply them, recursive or
            // not.)
            E.Resolution = IdentExpr::IdentResolution::RecursiveCall;
        }
    }

    // EP §6.4.7: active schema discriminant bindings.  Most contexts that
    // populate this map (resolving a schema's own body, or an undiscriminated
    // schema's probe) have no matching symbol-table entry for the name at
    // all, so TyInt -- an ordinal value good enough for compile-time bound
    // folding -- is the best available answer there.  But `with` over a
    // schema instance (pushWithScope) ALSO defines a real Const symbol
    // alongside the binding, carrying the discriminant's true declared type;
    // prefer that one, the same fallback checkField already uses, so e.g. a
    // char- or boolean-typed discriminant does not masquerade as an integer
    // for the rest of the with-body the way it did before (issue #18).
    if (!ActiveSchemaBindings_.empty()) {
        auto It = ActiveSchemaBindings_.find(toLower(E.Name));
        if (It != ActiveSchemaBindings_.end()) {
            if (Symbol* S = Symtab.lookup(E.Name);
                    S && S->Kind == SymbolKind::Const && S->Ty && !S->Ty->isError())
                return S->Ty;
            return TyInt;
        }
    }

    Symbol* Sym = Symtab.lookup(E.Name);
    if (!Sym) {
        if (checkRealModeDosName(E.Name, E.Loc)) return TyErr;
        error(E.Loc, diag::err_undefined_identifier, {E.Name});
        return TyErr;
    }
    Sym->Referenced = true;
    switch (Sym->Kind) {
        case SymbolKind::Var:
        case SymbolKind::VarParam:
        case SymbolKind::Const:
        case SymbolKind::EnumValue:
            // Turbo untyped parameter (`procedure P(var x)`) used bare --
            // Sym->Ty is deliberately null (ParamGroup::Type's own comment).
            // The two legal uses of one bypass checkIdent entirely rather
            // than reaching this point: the operand of a variable typecast
            // (checkTypeCast's own comment) and a direct relay to another
            // untyped formal (checkCallArgs's own comment).  Every other use
            // -- arithmetic, field/index access, an ordinary argument, a
            // bare read -- lands here and gets a real diagnostic instead of
            // silently becoming TyErr (which, being the generic error
            // sentinel, would have let every one of those through unchecked
            // rather than rejecting them the way this feature requires).
            if (!Sym->Ty && (Sym->Kind == SymbolKind::Var
                             || Sym->Kind == SymbolKind::VarParam)) {
                error(E.Loc, diag::err_untyped_param_bare_use, {E.Name});
                return TyErr;
            }
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
            if (Sym->IsFunction) {
                // Every OTHER dialect-restricted builtin either requires at
                // least one argument (so a bare, parenthesis-less use of it
                // can only ever be a plain undefined-identifier, since
                // Symtab.lookup still finds the always-registered Builtin
                // Symbol here and this arm is reached regardless) or, like
                // eof/eoln, is registered in every dialect and so has no
                // gating to skip.  Random and ParamCount (Builtins.def,
                // both -std=turbo only, landed independently and at the
                // same time) are the first two EXCEPTIONS to both: each is
                // dialect-restricted AND takes zero arguments, so `x :=
                // Random;`/`writeln(ParamCount)` under -std=iso7185/
                // -std=iso10206 reached this generic path with nothing here
                // to reject either, silently returning Sym->ReturnType and
                // letting CodeGen's own bare-call case for each (CGExprCore.
                // cpp, right beside eof/eoln's identical bare-call handling)
                // actually emit the call -- a silent miscompile, not a
                // diagnostic, for a name the parenthesized CallExpr path
                // (checkCallExpr's own checkEPOnly call) already correctly
                // refuses.  Same check, reused here rather than reinvented,
                // closes it for both and for any future dialect-restricted,
                // zero-argument Func this table gains.
                if (!checkEPOnly(*Sym, E.Loc)) return TyErr;
                return Sym->ReturnType ? Sym->ReturnType : TyErr;
            }
            error(E.Loc, diag::err_builtin_proc_as_value, {E.Name});
            return TyErr;
        case SymbolKind::TypeAlias:
        case SymbolKind::Schema:  // EP §6.4.7: schema name cannot be used as a value
            error(E.Loc, diag::err_type_name_as_value, {E.Name});
            return TyErr;
        case SymbolKind::Label:
            error(E.Loc, diag::err_label_as_value, {E.Name});
            return TyErr;
        // Turbo Tier 5: a Method symbol is registered under a synthetic
        // "type.method" composite key (SymbolKind::Method's own comment,
        // SymbolTable.h), which contains a '.' no ordinary identifier can --
        // so Symtab.lookup(E.Name) here, keyed by a bare IdentExpr::Name,
        // can never actually find one.  An unqualified 'M' inside a method
        // body meaning "call my own method M" (or an ancestor's) is a
        // different, later lookup this codebase does not perform yet --
        // item 3+'s job, once a method body's own scope exists at all (see
        // checkMethodBody's own comment, Sema.h, for why Phase 5b does not
        // even attempt to check a method body's statements today).
        case SymbolKind::Method:
            error(E.Loc, diag::err_undefined_identifier, {E.Name});
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
        ArrTy = schemaUnderlying(ArrTy);
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
    if (isVarStringLike(ArrTy.get())) {
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return TyChar;
    }
    // Turbo string[N]: s[i] has char components too -- but 0-based, with
    // s[0] a deliberate aliasing exception onto the string's own one-byte
    // length prefix (see CGIndexAccess.cpp's own s[0] comment for the
    // in-place-truncation idiom that exists for).  Sema does not itself
    // bound-check EITHER dialect's string index (VarString's arm just above
    // doesn't either -- that is CodeGen's job, via RangeCheckGuards), so the
    // only thing this arm needs to decide, like VarString's, is the element
    // TYPE.  A separate arm rather than widening isVarStringLike's own test
    // above: ShortString is never VarString-like (isVarStringLike is false
    // for it by construction), and the two dialects' index BOUNDS differ
    // (1..current-length for VarString, 0..declared-capacity for
    // ShortString) even though the element type does not.
    if (isShortStringLike(ArrTy.get())) {
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return TyChar;
    }
    // Turbo: `p[i]` on a PChar-like pointer indexes through it, zero-based,
    // with no declared extent to range-check against -- the same "pointee is
    // Char" gate checkBinary's pointer-arithmetic case uses (see
    // isCharPointerType's comment in Type.h for the fpc -Mtp field-practice
    // trail), and the same Opts.turbo() gate keeping an ISO/EP `^char`
    // untouched.  Confirmed against real fpc: a user's own `type P = ^Char`
    // indexes exactly like PChar does, both for a read and -- since
    // isLValue/checkAssign route straight back through this same function --
    // `p[0] := 'H'` as a write.
    if (Opts.turbo() && isCharPointerType(*ArrTy)) {
        if (!IdxTy->isError() && !IdxTy->isOrdinal())
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        return TyChar;
    }
    // EP §6.7.3.7: conformant arrays are indexed like regular arrays.
    if (ArrTy->Kind == TypeKind::ConformantArray) {
        const bool IdxNotOrdinal = !IdxTy->isError() && !IdxTy->isOrdinal();
        if (IdxNotOrdinal)
            error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
        // ISO §6.5.3.2: same assignment-compatibility check the plain-Array
        // branch below makes against IndexType, but against the ordinal type
        // named in this dimension's index-type-specification -- a conformant
        // array leaves the BOUNDS unknown until the call, not the index type.
        const std::shared_ptr<Type> IdxSpecTy = !ArrTy->ConformantBounds.empty()
            ? ArrTy->ConformantBounds[0].OrdType : nullptr;
        if (!IdxNotOrdinal && !IdxTy->isError() && IdxSpecTy
            && !IdxSpecTy->isError() && !isAssignCompatible(*IdxSpecTy, *IdxTy))
            error(E.Loc, diag::err_index_type_mismatch, {IdxTy->Name, IdxSpecTy->Name});
        return ArrTy->ElemType ? ArrTy->ElemType : TyErr;
    }
    if (ArrTy->Kind != TypeKind::Array) {
        error(E.Loc, diag::err_subscript_non_array, {ArrTy->Name});
        return TyErr;
    }
    const bool IdxNotOrdinal = !IdxTy->isError() && !IdxTy->isOrdinal();
    if (IdxNotOrdinal)
        error(E.Loc, diag::err_index_not_ordinal, {IdxTy->Name});
    // ISO §6.5.3.2: index expression must be assignment-compatible with the
    // declared index type.  Skipped once err_index_not_ordinal has already
    // fired for this same index expression -- a non-ordinal index is never
    // assignment-compatible with an ordinal index type either, so the second
    // check would only restate the same root cause as a separate diagnostic.
    if (!IdxNotOrdinal && !IdxTy->isError() && ArrTy->IndexType
        && !ArrTy->IndexType->isError()
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
        // rejected the varying non-array case, so this is safe.  The body may
        // itself be another schema instantiation (EP §6.4.7 lets it) — a
        // schema whose body is a schema whose body is the record still has
        // its fields, so the question is asked of the record schemaUnderlying
        // reaches, not the immediate body.
        if (Undiscriminated && schemaUnderlying(RecTy->SchemaBody)->Kind != TypeKind::Record) {
            error(E.Loc, diag::err_schema_not_a_discriminant, {RecTy->Name, E.Field});
            return TyErr;
        }
        RecTy = schemaUnderlying(RecTy);
    }

    // Turbo Tier 5, Cluster A item 7: 'Obj.Field'/'P^.Field' outside a
    // method body ('P^' already unwrapped to the Object pointee by
    // checkDeref by the time it reaches here, exactly like a method-call
    // receiver -- see checkMethodCall's own comment).  RecordFields is
    // already the flattened ancestor-then-own list every other Tier-5 field
    // reader relies on (see its own comment, Type.h), so this reuses
    // fieldByName below completely unchanged from the Record case; the only
    // new work is the Kind check itself and the private-visibility gate.
    if (RecTy->Kind == TypeKind::Object) {
        const Type::Field* F = RecTy->fieldByName(E.Field);
        if (!F) {
            error(E.Loc, diag::err_object_no_such_field, {RecTy->Name, E.Field});
            return TyErr;
        }
        if (F->IsPrivate && F->DeclaringModule != CurrentUnit_)
            error(E.Loc, diag::err_object_private_field, {RecTy->Name, E.Field});
        return F->Ty ? F->Ty : TyErr;
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
    if (PtrTy->Kind == TypeKind::Pointer) {
        auto Pointee = PtrTy->PointeeType;
        if (!Pointee) return TyErr;
        // EP §6.4.7: `p^` for a pointer to an undiscriminated schema is a value
        // of the schema's BODY -- the discriminants only say which member of
        // the family it is.  An array body is left as the schema, because
        // indexing recovers its bounds from the header and the index path
        // already looks through; anything else has to read as what it is, or
        // `q^ := 'hi'` for a `^string` is an assignment to a type called
        // "string" that no string rule applies to.  Codegen recovers the schema
        // from the pointer rather than from here.
        // Only a string body, and deliberately: a record body keeps the schema
        // type because that is what carries the discriminants as pseudo-fields
        // and what a schema parameter is matched against, so handing back the
        // bare record loses `q^.id` and makes `bump(q^)` the wrong type.  The
        // body may itself be another schema instantiation (EP §6.4.7), so the
        // question is asked of what it underlies to, not of the immediate hop
        // -- `C(n) = B(n)` for `B(m) = string(m)` is a string body too, one
        // level further down.
        if (Pointee->Kind == TypeKind::Schema && Pointee->SchemaBody) {
            auto Underlying = schemaUnderlying(Pointee->SchemaBody);
            if (Underlying->Kind == TypeKind::VarString)
                return Underlying;
        }
        return Pointee;
    }
    if (PtrTy->Kind == TypeKind::File) {
        // ISO §6.5.5: f^ for a file variable accesses the file buffer
        // variable.  Its type is the file's component type; for text files
        // it is char.  -std=turbo has no buffer variable at all -- it
        // replaces the whole get/put/page file model (already refused by
        // checkEPOnly, above the call sites that reach here) with
        // Assign/Seek, and the parser places no dialect boundary of its own
        // between an ordinary pointer's `p^` and a file's `f^`, so this is
        // the one place actually able to tell the two apart before codegen
        // sees a Turbo program dereference a file and finds no buffer to
        // read.  Narrowly scoped to PtrTy->Kind == File: an ordinary
        // Pointer, just above, and a Schema pointer's discriminant-header
        // form, inside that same arm, both return before ever reaching here
        // and so are completely unaffected by this check.
        if (Opts.turbo()) {
            error(E.Loc, diag::err_turbo_file_buffer_var);
            return TyErr;
        }
        return PtrTy->ElemType ? PtrTy->ElemType : TyChar;
    }
    error(E.Loc, diag::err_deref_non_pointer, {PtrTy->Name});
    return TyErr;
}

namespace {
// Full definition, with its rationale, is below with the other schema-
// identity helpers (checkUserDefinedCall's neighborhood); forward-declared
// here so Sema::checkBinary's pointer-equality check (issue #407) can reach
// it without moving that comment block up past everything it documents.
bool schemaInstMatch(const Type& A, const Type& B);
} // anonymous namespace

// Turbo Tier 5, Cluster A item 7: full definition, with its rationale, is
// below with isAssignCompatible (its main user); forward-declared here so
// Sema::checkCallArgs's var-parameter covariance check, just below, can
// reach it without moving that comment block up past everything it
// documents -- the same reason schemaInstMatch is forward-declared above.
static bool objectIsOrDescendsFrom(const Type& Descendant, const Type& Ancestor);

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
        case TokenKind::Plus: {
            // Turbo string[N] concatenation -- decided FIRST and separately
            // from the EP block below, which classifies every operand with
            // isStringConcatOperand (isVarStringLike/String/isCharStringType)
            // and, whenever it fires, always returns an EP-shaped VarString
            // result via Type::makeVarString.  isVarStringLike is false for
            // ShortString by construction (see its own comment, Type.h), so
            // a ShortString operand pair would otherwise fall all the way
            // through to the plain-numeric '+' checked further down and be
            // refused as non-numeric.  Gated on at least one operand
            // actually being a ShortString, so a plain literal/char/
            // char-array concatenation with NO ShortString involved is
            // completely unaffected and still reaches the EP block exactly
            // as before.
            auto isShortStrConcatOperand = [](const std::shared_ptr<Type>& T) {
                return isShortStringLike(T.get()) || T->Kind == TypeKind::Char
                    || T->Kind == TypeKind::String;
            };
            if ((isShortStringLike(Lt.get()) || isShortStringLike(Rt.get()))
                    && isShortStrConcatOperand(Lt) && isShortStrConcatOperand(Rt)) {
                auto cap = [](const std::shared_ptr<Type>& T) -> int64_t {
                    if (isShortStringLike(T.get())) return T->StrCapacity;
                    if (T->Kind == TypeKind::Char)   return 1;
                    return PlangMaxStringCapacity; // a String operand's
                                                    // length is not known
                                                    // until run time
                };
                // A ShortString's capacity can never exceed 255 -- the
                // one-byte length prefix's own ceiling (see plang_sstr.cpp)
                // -- regardless of what the two operands' capacities sum to;
                // CodeGen's plang_sstr_concat clamps identically at run time,
                // so the declared result type and what the runtime actually
                // does agree.
                const int64_t sum = cap(Lt) + cap(Rt);
                return Type::makeShortString(std::min<int64_t>(sum, PlangMaxStringCapacity));
            }
            // EP §6.8.3.6: string concatenation.  ISO 10206 §6.4.3.3.1's note
            // is explicit that a STRING-TYPE -- the category covering the
            // fixed-string-type (ISO §6.4.3.2's packed array[1..n] of char)
            // as much as the canonical one -- is usable with the
            // concatenation operator, because "each string-type value is a
            // value of the canonical-string-type" (§6.4.3.3.1): a
            // fixed-string-type operand already satisfies table 7's
            // "canonical-string-type" operand column by that coercion rule.
            // isVarStringLike/Kind==String were missing isCharStringType, the
            // same sibling gap length/substr/trim/index had.
            //
            // isStringConcatOperand below classifies each operand on its own
            // terms (VarString-like, canonical String, or a fixed
            // char-string-type); the concatenation is only accepted once
            // BOTH operands independently qualify (a lone Char pairs with
            // any of those, or with another Char under CharConcatenation).
            // Accepting the pair off of only ONE operand's kind -- as a
            // stray `isVarStringLike(Lt) || isVarStringLike(Rt)` would -- let
            // a genuinely mismatched pair (e.g. 'hello' + aRecord) reach
            // CodeGen, which has no lowering for that and crashes instead of
            // Sema cleanly rejecting it.
            auto isStringConcatOperand = [](const std::shared_ptr<Type>& T) {
                return isVarStringLike(T.get()) || T->Kind == TypeKind::String
                    || (!T->isError() && isCharStringType(*T));
            };
            bool LOk = isStringConcatOperand(Lt);
            bool ROk = isStringConcatOperand(Rt);
            bool LChar = Lt->Kind == TypeKind::Char;
            bool RChar = Rt->Kind == TypeKind::Char;
            if ((LOk && ROk)
                || (LOk && RChar)
                || (LChar && ROk)
                // EP §6.8.3.2: a char is a string-compatible operand of '+', so
                // two of them concatenate rather than failing as non-numeric.
                || (Opts.has(LangOptions::Feature::CharConcatenation)
                        && LChar && RChar)) {
                auto cap = [](const std::shared_ptr<Type>& T) -> int64_t {
                    // A capacity fixed by a discriminant is the probe's here,
                    // so it is treated the way an unbounded string is: the
                    // result of a concatenation is a temporary, and the widest
                    // one plang has is the honest bound for it.
                    if (isVarStringLike(T.get()))
                        return T->ExtentVaries ? PlangMaxStringCapacity
                                               : T->StrCapacity;
                    if (!T->isError() && isCharStringType(*T))
                        return charStringLength(*T);
                    if (T->Kind == TypeKind::Char)      return 1;
                    return PlangMaxStringCapacity; // unbounded string
                };
                return Type::makeVarString(cap(Lt) + cap(Rt));
            }
        }
            [[fallthrough]];
        case TokenKind::Minus: {
            // Turbo: PChar-like pointer arithmetic -- `p + n`, `p - n` and
            // `p1 - p2` -- intercepted before the generic numeric gate below,
            // the same shape the string-concat block above and the set
            // union/difference/intersection block just below already use.
            //
            // Gated on Opts.turbo() *and* isCharPointerType, not on identity
            // to the PChar singleton: see isCharPointerType's own comment
            // (Type.h) for the empirical fpc -Mtp field-practice trail this
            // follows -- a user's own `type P = ^Char` gets exactly the same
            // arithmetic PChar does on a real Turbo/Delphi/FPC compiler.
            // Opts.turbo() is what keeps an ISO 7185/EP `^char` untouched:
            // every dialect that can declare one has Opts.turbo() false.
            //
            // Checked against real fpc (not just assumed): `p + q` (two
            // pointers), `n - p` and `n + p` (integer first) are ALL refused
            // by fpc 3.2.2 -- only pointer-then-integer commutes here, unlike
            // ordinary '+'.
            if (Opts.turbo() && (isCharPointerType(*Lt) || isCharPointerType(*Rt))) {
                const bool LPtr = isCharPointerType(*Lt);
                const bool RPtr = isCharPointerType(*Rt);
                if (E.Op == TokenKind::Plus) {
                    if (LPtr && !RPtr && Rt->isIntegral()) return Lt;
                } else { // Minus
                    if (LPtr && RPtr) return TyInt; // p1 - p2: element count
                    if (LPtr && !RPtr && Rt->isIntegral()) return Lt; // p - n
                }
                error(E.Loc, diag::err_op_numeric,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
        }
            [[fallthrough]];
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
            // Both operands genuinely participate in a div/mod result (unlike
            // shl/shr's shift count, see below) -- confirmed against real
            // `fpc -Mtp`: `Int64Var div ByteVar` computes and answers at
            // Int64's own width, not Byte's or the dialect default's.
            return commonIntType(*Lt, *Rt);

        case TokenKind::And:
        case TokenKind::Or:
            // Turbo: `and`/`or` are overloaded -- on two INTEGER operands
            // they are bitwise (`5 and 3` = 1), not logical, while on two
            // Boolean operands they stay logical, same as ISO/EP.
            // CGBinaryOps::emitBinary's own comment says the two dispatch
            // orders have to agree: operand type decides bitwise vs.
            // Boolean here in Sema the same way it decides bitwise vs.
            // short-circuit there in CodeGen.
            if (Opts.turbo() && Lt->isIntegral() && Rt->isIntegral())
                return commonIntType(*Lt, *Rt);
            [[fallthrough]];
        case TokenKind::AndThen:  // EP §6.8.3.3
        case TokenKind::OrElse:   // EP §6.8.3.3
            if (Lt->Kind != TypeKind::Boolean || Rt->Kind != TypeKind::Boolean) {
                // err_op_boolean_or_integer only ever applies to And/Or:
                // AndThen/OrElse are EP-only tokens, and EP is never Turbo,
                // so Opts.turbo() is always false whenever this shared arm
                // is reached for one of them.
                if (Opts.turbo())
                    error(E.Loc, diag::err_op_boolean_or_integer,
                          {opSpelling(E.Op), Lt->Name, Rt->Name});
                else
                    error(E.Loc, diag::err_op_boolean,
                          {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            return TyBool;

        // Turbo `xor`: overloaded the identical way `and`/`or` are --
        // bitwise on two Integer operands, logical (exclusive-or) on two
        // Boolean ones.  Only ever reached under -std=turbo: the scanner is
        // the sole gate on the Xor token (DIALECT_KEYWORD, D_Turbo alone),
        // the same way an EP-only keyword never reaches Sema under ISO 7185
        // (see checkUnary's At case for the identical reasoning).  Unlike
        // and/or, xor never short-circuits -- both operands always matter to
        // an exclusive-or result, and CGBinaryOps has no dispatch for it to
        // agree with -- so there is no AndThen/OrElse-shaped sibling case.
        case TokenKind::Xor:
            if (Lt->isIntegral() && Rt->isIntegral())
                return commonIntType(*Lt, *Rt);
            if (Lt->Kind != TypeKind::Boolean || Rt->Kind != TypeKind::Boolean) {
                error(E.Loc, diag::err_op_boolean_or_integer,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            return TyBool;

        // Turbo `shl`/`shr`: integer-only shift operators with no ISO/EP
        // equivalent at all -- only ever reached under -std=turbo (same
        // scanner gate as Xor above).
        case TokenKind::Shl:
        case TokenKind::Shr:
            if (!Lt->isIntegral() || !Rt->isIntegral()) {
                error(E.Loc, diag::err_op_integer,
                      {opSpelling(E.Op), Lt->Name, Rt->Name});
                return TyErr;
            }
            // Unlike Div/Mod/Xor/And/Or just above, the RIGHT operand here is
            // a shift COUNT, not a value that shares in the result the way
            // both operands of a div or xor do -- confirmed against real
            // `fpc -Mtp`: `Int64Var shl ByteCount` answers at Int64's own
            // width regardless of the count's own (narrower) type, and
            // `ByteVar shl ByteCount` answers WIDER than Byte's own 8 bits
            // (`fpc`'s own native int width; here, the dialect's own default
            // Integer width) rather than masking the shift down to 8 bits --
            // i.e. a narrow left operand is promoted UP to at least the
            // dialect default the same way a narrower-than-int C operand is,
            // while a left operand already at or above that default keeps
            // its own width exactly (an Int64 never narrows to 16 here).
            // Signedness travels with the LEFT operand alone, not
            // Lt.IsSigned && Rt.IsSigned the way commonIntType's equal-width
            // tie-break works: `ByteVar shl N` stays unsigned even though a
            // literal shift count is the dialect's own signed default int,
            // confirmed the same way (`fpc -Mtp`'s Byte shl Byte(31) prints
            // as unsigned, not as a negative promoted int).
            return Ctx_.getInt(std::max(Lt->Width, TyInt->Width), Lt->IsSigned);

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
                return T.Kind == TypeKind::String || isVarStringLike(&T)
                    || T.Kind == TypeKind::Char   || isCharStringType(T);
            };
            // Turbo string[N]'s own sibling of isStringLike just above --
            // kept SEPARATE (an added OR at each of isStringLike's two use
            // sites below) rather than folded into it: isStringLike is a
            // shared EP/ISO predicate whose Char/isCharStringType/String
            // arms are not ShortString's to redefine, and ShortString's
            // comparison RESULT (prefix, shorter-is-less; plang_sstr_eq and
            // siblings) is a genuinely different runtime question from
            // isStringLike's own space-padded EP one even where, as here,
            // the two happen to share which OPERAND KINDS qualify.
            auto isShortStrLike = [](const Type& T) {
                return isShortStringLike(&T) || T.Kind == TypeKind::Char
                    || T.Kind == TypeKind::String;
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
                         && Rt->Kind == TypeKind::Pointer && Lt != Rt
                         // Pointer types are interned by pointee identity
                         // (TypeContext::getPointer), so Lt != Rt ordinarily
                         // does mean the two domains differ -- except a
                         // SchemaInstance pointee is deliberately never
                         // interned (see schemaInstMatch), so two aliases
                         // naming the identical schema instantiation mint two
                         // distinct Pointer objects here too.  Issue #407.
                         //
                         // Turbo's generic `Pointer` has no PointeeType at
                         // all (TypeContext::getGenericPointer) -- the guard
                         // below requires BOTH sides to have one before the
                         // domains are even asked about, so a comparison
                         // with a generic Pointer on either side never
                         // reaches (and cannot fail) the identity check,
                         // the same blanket pass Nil gets a few lines above.
                         && Lt->PointeeType && Rt->PointeeType
                         && !(isIdenticalType(Lt->PointeeType, Rt->PointeeType)
                              || schemaInstMatch(*Lt->PointeeType, *Rt->PointeeType)))
                    error(E.Loc, diag::err_cannot_compare, {Lt->Name, Rt->Name});
                return TyBool;
            }
            // ISO §6.7.2.5 lists the comparable types, and arrays, records and
            // files are not among them.  Matching kinds alone let these
            // through to codegen, which has no instruction for them.
            auto isUncomparable = [&](const Type& T) {
                if (isStringLike(T) || isShortStrLike(T)) return false;
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
                   || (isStringLike(*Lt) && isStringLike(*Rt))
                   || (isShortStrLike(*Lt) && isShortStrLike(*Rt));
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
                //
                // Ctx_.getSet is a bare interning factory with no width
                // opinion of its own -- the OTHER caller (SemaType.cpp, for a
                // named `set of Base`) checks the base type before ever
                // calling it, and synthesizing a set type here has to be held
                // to the same limit or a >256-value ordinal (an enum, most
                // plausibly) silently truncates in the bitmask instead of
                // being reported: `e in [v256]` read as false for e = v256.
                checkSetBaseRange(*Lt, E.Loc);
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
    // Turbo `@g` where g bare-names a routine: real Turbo Pascal's `@` on a
    // procedure/function identifier always yields a reference to the
    // routine itself, regardless of where the result is used -- unlike
    // `f := g` (checkAssign's own arm), this needs no destination-type
    // context to disambiguate, since '@' is itself the unambiguous marker.
    // Decided before the generic checkExpr(*E.Operand) just below, which
    // would otherwise apply checkIdent's ordinary rule to a bare Proc-kind
    // identifier -- an implicit zero-argument call for a function, or
    // err_proc_as_value outright for a procedure -- to what is written here
    // as this operator's OWN operand, not an ordinary read.
    if (E.Op == TokenKind::At) {
        if (auto* Id = llvm::dyn_cast<IdentExpr>(E.Operand.get());
                Id && isRoutineNameCandidate(*Id))
            return checkRoutineValue(*Id);
    }

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
            // Turbo overloads 'not' like 'and'/'or'/'xor': bitwise
            // two's-complement negation on an Integer operand, logical
            // negation on a Boolean one.  ISO/EP have no bitwise 'not' --
            // an Integer operand there is the same error it always was.
            //
            // A plain Integer at T's own Width/IsSigned, not unconditionally
            // TyInt and not T itself: unlike shl/shr, 'not' does not promote
            // a narrow operand up to the dialect default -- confirmed
            // against real `fpc -Mtp`, `not Byte(1)` stays an 8-bit
            // complement (254), not a promoted-then-negated -2 -- and
            // CGBinaryOps::emitUnary's Not case already computes it that way
            // (CreateNot at the operand's own LLVM width, no
            // e.ResolvedType->Width re-coercion the way Shl/Shr's codegen
            // needs); only the Sema-reported result TYPE was wrong, the same
            // "unconditionally TyInt regardless of the operand's actual
            // Width/IsSigned" bug the sized-integer ladder exposed in
            // checkBinary's own arms (see commonIntType's comment).  Not T
            // itself, so a computed result never carries a SUBRANGE's own
            // narrower bounds the way the source variable's declared type
            // might (`not` of a `1..100` subrange is not itself `1..100`) --
            // commonIntType(*T, *T) is the same "mint a plain Integer at
            // this Width/IsSigned" primitive commonIntType's own equal-width
            // branch already is, just applied to one operand twice.
            if (T->Kind == TypeKind::Boolean) return TyBool;
            if (Opts.turbo() && T->isIntegral()) return commonIntType(*T, *T);
            if (Opts.turbo())
                error(E.Loc, diag::err_not_requires_boolean_or_integer, {T->Name});
            else
                error(E.Loc, diag::err_not_requires_boolean, {T->Name});
            return TyErr;
        case TokenKind::At:
            // Turbo `@x`: only ever reached under -std=turbo -- the scanner
            // is the sole gate on the At token (see its '@' dispatch), the
            // same way an EP-only keyword never reaches Sema under ISO 7185.
            // The operand has to be an addressable variable, the same
            // requirement checkExpr's 'var'-parameter check (isLValue) makes
            // of a call argument; '@(1+1)' or '@f(x)' has no address to take.
            if (!isLValue(*E.Operand)) {
                error(E.Loc, diag::err_addrof_requires_variable, {T->Name});
                return TyErr;
            }
            return Ctx_.getPointer(T);
        default:
            error(E.Loc, diag::err_unknown_unop);
            return TyErr;
    }
}

bool Sema::checkBuiltinArity(BuiltinID ID, const std::string& LowerName,
                             SourceLocation Loc, size_t NumArgs) {
    // ISO §6.6.6 and EP §6.7.6 fix the shape of each required function, and
    // Builtins.def is where that shape is written -- the same entry that
    // declares the name.  A Max of -1 is deliberately unconstrained: the
    // required procedures are genuinely variadic (write) or already checked
    // where they are lowered.
    if (ID == BuiltinID::None) return true;
    const auto [Min, Max] = builtinArity(ID);
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

namespace {
/// Turbo Pascal 7's real-mode DOS surface: segment/offset pointer
/// manipulation, raw memory/port access, heap-internals variables, and
/// low-level DOS/BIOS interrupt calls.  None of it has any meaning on
/// plang's flat-address-space, 64-bit Linux/macOS target.  Spellings verified
/// against the Turbo Pascal 7.0 Language Guide / Programmer's Reference and
/// (for the modern-FPC survivors: Seg, Ofs, CSeg, DSeg, SSeg, SPtr, Ptr)
/// the Free Pascal RTL's System-unit reference.
///
/// Deliberately excluded (see DiagnosticSemaKinds.def's comment and the
/// callers of checkRealModeDosName): SwapVectors, GetCBreak, SetCBreak,
/// GetVerify, SetVerify -- real TP code calls these unconditionally around
/// Exec, so rejecting them would break programs that would otherwise run
/// fine.  A later task makes the Dos unit accept-and-no-op them; this one
/// must not touch them at all, in either direction.
///
/// The Overlay unit's manager routines and its OvrResult status variable are
/// included as "a handful of overlay-related names" per the task that added
/// this list; OvrCodeList/OvrDebugPtr (obscure internal-use variables) and
/// the ovrOk/ovrError/... integer error-code constants were deliberately
/// left off -- lower-risk to leave as ordinary undefined identifiers, and
/// less certain from available references.
constexpr std::string_view RealModeDosNames[] = {
    // Segment/offset pointers.
    "seg", "ofs", "cseg", "dseg", "sseg", "sptr", "ptr",
    // Raw memory and I/O-port arrays.
    "mem", "memw", "meml", "port", "portw",
    // CPU/FPU identification and the program-segment-prefix segment.
    "test8086", "test8087", "prefixseg",
    // Heap internals.
    "heaporg", "heapptr", "heapend", "freelist",
    "mark", "release", "memavail", "maxavail",
    // DOS/BIOS interrupt calls.
    "intr", "msdos", "getintvec", "setintvec", "keep",
    // Overlay manager (unit Overlay).
    "ovrinit", "ovrinitems", "ovrclearbuf", "ovrgetbuf", "ovrsetbuf",
    "ovrgetretry", "ovrsetretry", "ovrresult",
};

bool isRealModeDosName(const std::string& Name) {
    const std::string Lo = toLower(Name);
    return std::ranges::find(RealModeDosNames, Lo) != std::ranges::end(RealModeDosNames);
}
} // namespace

bool Sema::checkRealModeDosName(const std::string& Name, SourceLocation Loc) {
    if (!Opts.turbo()) return false;
    if (!isRealModeDosName(Name)) return false;
    error(Loc, diag::err_turbo_real_mode_facility, {Name});
    return true;
}

bool Sema::checkEPOnly(const Symbol& Sym, SourceLocation Loc) {
    // The dialect test already happened, at registration, against the mask in
    // Builtins.def.  Asking extendedPascal()/turbo() again here would be a
    // second answer to the same question -- and Assert (Dialects = TP) is
    // exactly the case where re-deriving it from Opts.Std would give the
    // wrong one: this Sema instance's own dialect is whichever ONE was
    // refused, never the name's, so it says nothing about which dialect
    // WOULD have accepted it.
    if (!Sym.NotInDialect) return true;
    // Four shapes, all of Builtins.def's actual Dialects masks other than
    // ALL (which never sets NotInDialect, so never reaches here).  Each picks
    // the DIAG that names the dialect(s) the name DOES belong to, since that
    // is a fact about Sym and is the same whichever dialect is active --
    // unlike "not available under -std=X", which used to hardcode iso7185
    // and was simply wrong once -std=turbo could refuse an Extended Pascal
    // name too (`card` under -std=turbo previously read "not available under
    // -std=iso7185", though iso7185 was never the dialect running it).
    const unsigned Dialects = builtinDialects(Sym.BuiltinKind);
    // Turbo-only (Assert, the first one, and the shape any future one
    // takes): only iso7185 or iso10206 can be active here.
    if (Dialects == LangOptions::D_Turbo) {
        error(Loc, diag::err_turbo_required_name, {Sym.Name});
        return false;
    }
    // ISO 7185's file-buffer model (get, put, page, pack, unpack): iso7185
    // and iso10206 both declare these, so only -std=turbo can be active
    // here, and turbo does not merely lack them -- it replaces them.
    if (Dialects == (LangOptions::D_ISO7185 | LangOptions::D_ISO10206)) {
        error(Loc, diag::err_turbo_file_model_name, {Sym.Name});
        return false;
    }
    // Extended-Pascal-and-Turbo (Halt, Length): only -std=iso7185, the sole
    // dialect missing from this two-bit mask, can be active here.
    if (Dialects == (LangOptions::D_ISO10206 | LangOptions::D_Turbo)) {
        error(Loc, diag::err_ep_turbo_required_name, {Sym.Name});
        return false;
    }
    // Extended Pascal alone (Card, and most of the rest of the EP block):
    // either iso7185 or turbo can be active here.
    error(Loc, diag::err_ep_required_name, {Sym.Name});
    return false;
}

// See the declaration (Sema.h) for why this exists.  Deliberately narrow:
// called only from checkCallExpr's SizeOf/High/Low arm, never from the
// ordinary checkExpr dispatch, so every other call site's identifier keeps
// meaning exactly what checkIdent already says it means.
std::shared_ptr<Type> Sema::resolveTypeArgOrValue(const ExprNode& Arg) {
    if (auto* Id = llvm::dyn_cast<IdentExpr>(&Arg)) {
        // The five primitive type keywords: parseSizeHighLowArg hands back a
        // synthetic IdentExpr carrying the keyword's own spelling for
        // exactly this argument position, since none of the five is ever
        // entered in the symbol table under its own name (they are lexer
        // keywords, not identifiers, and never reach ordinary name lookup).
        const std::string Lo = toLower(Id->Name);
        std::shared_ptr<Type> Primitive;
        if      (Lo == "integer") Primitive = TyInt;
        else if (Lo == "real")    Primitive = TyReal;
        else if (Lo == "boolean") Primitive = TyBool;
        else if (Lo == "char")    Primitive = TyChar;
        else if (Lo == "string")  Primitive = TyStr;
        if (Primitive) {
            Id->IsTypeArgument = true;
            Id->ResolvedType = Primitive;
            return Primitive;
        }
        // An ordinary identifier that may name a TYPE (Byte, a user's own
        // TMyRecord, ...) rather than a variable.  checkIdent's ordinary
        // rule exists precisely to refuse a type name used as a value
        // (err_type_name_as_value) -- here, naming a type is exactly what
        // is wanted, so a TypeAlias symbol is read directly, bypassing
        // checkIdent rather than teaching it a context it cannot see.
        // Schema is deliberately not matched here: schemas are EP §6.4.7,
        // and EP and Turbo are different -std= values, so a Schema symbol
        // can never actually reach this arm under -std=turbo (the only
        // dialect SizeOf/High/Low are declared for -- Builtins.def).
        if (Symbol* Sym = Symtab.lookup(Id->Name);
                Sym && Sym->Kind == SymbolKind::TypeAlias) {
            Sym->Referenced = true;
            Id->UserDeclared = true;
            Id->IsTypeArgument = true;
            Id->ResolvedType = Sym->Ty ? Sym->Ty : TyErr;
            return Id->ResolvedType;
        }
    }
    // Otherwise this is an ordinary value expression: SizeOf(x) means the
    // size of x's own type, and High(arr)/Low(arr) mean the bounds of
    // arr's own index type (checked by the caller once this returns).
    return checkExpr(Arg);
}

std::shared_ptr<Type> Sema::checkCallExpr(const CallExpr& E) {
    Symbol* Sym = Symtab.lookup(E.Name);
    if (!Sym) {
        // Seg(x), Ofs(x) and the two-argument Ptr(seg, ofs) all require
        // parens, so they parse as a CallExpr and reach here rather than
        // checkIdent -- an expression-context use just as much as `Mem[...]`
        // is, only spelled with a call instead of an index.
        if (checkRealModeDosName(E.Name, E.Loc)) return TyErr;
        error(E.Loc, diag::err_undefined_function, {E.Name});
        return TyErr;
    }
    if (Sym->Kind == SymbolKind::Builtin) {
        E.ResolvedBuiltin = Sym->BuiltinKind;
        std::string Lo = toLower(E.Name);

        // Mirror of checkCallStmt's err_func_as_statement check, in the
        // other direction: a builtin PROCEDURE has no result, so calling one
        // where an expression is expected is exactly what
        // checkUserDefinedCall already refuses for a user-defined procedure
        // via err_proc_cannot_return_value.  Builtins skipped that check —
        // Sym->ReturnType is null for a procedure, so this fell through
        // every special-cased builtin below to the generic `return
        // Sym->ReturnType ? ... : TyErr` at the end of this arm, handing
        // back TyErr with no diagnostic at all.  Sema recorded no error, so
        // the driver went on to CodeGen, which had a call to a void builtin
        // where a value was expected and trapped (issue #222).
        if (!Sym->IsFunction) {
            error(E.Loc, diag::err_proc_cannot_return_value, {E.Name});
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyErr;
        }

        if (!checkEPOnly(*Sym, E.Loc)) {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyErr;
        }
        if (!checkBuiltinArity(Sym->BuiltinKind, Lo, E.Loc, E.Args.size())) {
            for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
            return TyErr;
        }
        // §6.6.6.5: eof and eoln read a file's status, so an argument, when
        // given, names the file being tested.  An ordinary variable was
        // accepted here with no check at all -- the diagnostic just below
        // this one fires only once the argument is already known to be a
        // file, so an integer or any other non-file type sailed past both.
        // CodeGen's lowering only recognizes a genuine file variable
        // (FileVars.isFileVar) and falls back to testing the standard input
        // file for anything else, so `eof(i)` for a plain integer i compiled
        // to testing INPUT's own eof status and silently discarded 'i'
        // (issue #261).
        if ((Lo == "eof" || Lo == "eoln") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (!ArgTy->isError() && ArgTy->Kind != TypeKind::File) {
                error(E.Args[0]->Loc, diag::err_file_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            // eoln asks whether the position is at a line marker, and only a
            // text file has those.  eof applies to any file and is not
            // restricted here.  isTextFile (Type.h) is dialect-aware -- see
            // its comment for why a `file of char` is text under ISO/EP but
            // not -std=turbo, and why an untyped `file` (also null
            // ElemType) is correctly refused here too now.
            if (Lo == "eoln" && ArgTy->Kind == TypeKind::File && !isTextFile(*ArgTy, Opts))
                error(E.Args[0]->Loc, diag::err_line_proc_not_text,
                      {Lo, ArgTy->Name});
            return TyBool;
        }
        // EP §6.7.6.6: position(f) and lastposition(f) report a value of f's
        // declared index type, and §6.7.6.5's empty(f) reports whether f
        // holds no components -- all three take a file argument the same
        // way eof/eoln do, just above, and had no check of their own at
        // all, so an ordinary variable sailed through unrejected.
        // CodeGen's lowering (FileVars.fileVarPtr) has no non-file fallback
        // the way eof/eoln's stdin fallback does: it handed the
        // wrong-typed variable's own address to the runtime as a
        // PascalFile*, segfaulting with no diagnostic (issue #417).
        if ((Lo == "position" || Lo == "lastposition" || Lo == "empty")
                && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (!ArgTy->isError() && ArgTy->Kind != TypeKind::File) {
                error(E.Args[0]->Loc, diag::err_file_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // TP-only: FilePos(f) / FileSize(f) -- see Builtins.def's own
        // comment for what these report and why.  Both take a typed or
        // untyped BINARY file (err_binary_file_required rejects Text, the
        // same way eoln's err_line_proc_not_text rejects a non-text file
        // just below) and answer with a genuine 64-bit Int64, overriding
        // Builtins.def's R_Int placeholder the same way abs/sqr's own
        // ArgTy override does just below.
        if ((Lo == "filepos" || Lo == "filesize") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (!ArgTy->isError()) {
                if (ArgTy->Kind != TypeKind::File) {
                    error(E.Args[0]->Loc, diag::err_file_argument, {Lo, ArgTy->Name});
                    return TyErr;
                }
                if (isTextFile(*ArgTy, Opts)) {
                    error(E.Args[0]->Loc, diag::err_binary_file_required,
                          {Lo, ArgTy->Name});
                    return TyErr;
                }
            }
            return Ctx_.getInt(64, /*Signed=*/true);
        }
        // TP-only: SeekEof(f) / SeekEoln(f) -- the CONSUMING counterparts
        // of Eof/Eoln (Builtins.def's own comment); Text-only, exactly the
        // restriction eoln already enforces just above.
        if ((Lo == "seekeof" || Lo == "seekeoln") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (!ArgTy->isError()) {
                if (ArgTy->Kind != TypeKind::File) {
                    error(E.Args[0]->Loc, diag::err_file_argument, {Lo, ArgTy->Name});
                    return TyErr;
                }
                if (!isTextFile(*ArgTy, Opts)) {
                    error(E.Args[0]->Loc, diag::err_line_proc_not_text,
                          {Lo, ArgTy->Name});
                    return TyErr;
                }
            }
            return TyBool;
        }
        // abs/sqr are polymorphic: return the argument's type.
        if ((Lo == "abs" || Lo == "sqr") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            // EP §6.7.6.2: abs(complex) → real; sqr(complex) → complex.
            if (ArgTy->Kind == TypeKind::Complex)
                return (Lo == "abs") ? TyReal : TyComplex;
            // ISO §6.6.6.2: abs and sqr take an integer-type or real-type
            // argument.  isOrdinal() also admits boolean, char and
            // enumerations -- ordinal but not numeric -- so this accepted
            // `abs(true)` and typed it as boolean (abs of an ordinal
            // returned the argument's own type unexamined) instead of
            // rejecting it.  The rejecting branch also reported nothing at
            // all: an argument that failed both checks (a string, record,
            // or set) produced TyErr with no diagnostic, so Sema recorded no
            // error and the driver went on to CodeGen with a call it cannot
            // lower (issue #261).
            if (!ArgTy->isError() && !ArgTy->isNumeric()) {
                error(E.Args[0]->Loc, diag::err_numeric_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return ArgTy;
        }
        // ISO §6.6.6.4: succ and pred stay in the argument's type, so
        // succ('a') is a char and succ(red) is the enumeration's next value.
        // EP §6.7.6.5 adds the two-argument form, which does not change this.
        if ((Lo == "succ" || Lo == "pred") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            std::shared_ptr<Type> StepTy;
            for (size_t I = 1; I < E.Args.size(); ++I) {
                auto T = checkExpr(*E.Args[I]);
                if (I == 1) StepTy = T;
            }
            if (E.Args.size() > 1 && !Opts.extendedPascal()) {
                error(E.Loc, diag::err_ep_two_arg_form, {Lo});
                return TyErr;
            }
            if (ArgTy->isError()) return TyErr;
            if (!ArgTy->isOrdinal()) {
                error(E.Loc, diag::err_ordinal_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            // EP §6.7.6.5: the step count k is a separate value from x and is
            // always of type integer, whatever x's type is -- succ(x, k)
            // does not walk k steps through x's own type the way succ(x)
            // walks one, so a char or boolean k has no more meaning than a
            // real one does.  This was never checked, so `succ(5, 'a')`
            // silently walked ord('a') steps (issue #261).
            if (StepTy && !StepTy->isError() && !StepTy->isIntegral()) {
                error(E.Args[1]->Loc, diag::err_step_argument_not_integer,
                      {Lo, StepTy->Name});
                return TyErr;
            }
            return ArgTy;
        }
        // ISO §6.6.6.4 (ord, chr) / §6.6.6.5 (odd): all three transfer between
        // an ordinal value and its ordinal position -- ord(x) is x's position,
        // chr(x) is the value at position x, odd(x) reads position x's low
        // bit -- so, like succ/pred just above, the argument has to be
        // ordinal. Unlike succ/pred none of the three were special-cased
        // here, so they fell through to the generic "check each argument,
        // trust the declared return type" path below with nothing stopping a
        // non-ordinal argument. CodeGen has nothing valid to lower one to
        // either: ord's case zext's its operand unconditionally, so
        // ord(1.5) reached the LLVM verifier as `zext double ... to i64`
        // and aborted the compiler instead of Sema reporting it (issue #212).
        if ((Lo == "ord" || Lo == "chr" || Lo == "odd") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (!ArgTy->isOrdinal()) {
                error(E.Loc, diag::err_ordinal_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // ISO §6.6.6.3: trunc and round convert a real value to an integer,
        // so the argument has to be numeric the same way sqrt/sin/... below
        // require -- and, unlike those, there is no complex extension for
        // either (EP does not give trunc/round a complex form).  Neither was
        // special-cased here, so they fell through with nothing to stop a
        // non-numeric argument: CodeGen's lowering (ToDouble, an
        // unconditional signed-int-to-double conversion) has no case for a
        // non-scalar type such as a string or record, and turns a char or
        // boolean's raw ordinal value into a number nobody asked for instead
        // of being rejected (issue #261).
        // TP-only: Int and Frac share this exact argument check with
        // trunc/round -- numeric, non-complex -- and need nothing more of
        // their own: unlike trunc/round (an ordinal Result, R_Int), each is
        // declared R_Real in Builtins.def, so the SAME
        // `Sym->ReturnType ? ... : TyErr` fallback just below already
        // answers TyReal for them without a dedicated arm the way Random's
        // does above.
        if ((Lo == "trunc" || Lo == "round" || Lo == "int" || Lo == "frac")
                && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (!ArgTy->isNumeric() || ArgTy->Kind == TypeKind::Complex) {
                error(E.Args[0]->Loc, diag::err_numeric_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // EP §6.7.6.7: substr/trim return the same string capacity as their
        // input.  ISO §6.4.3.2's other string shape -- a packed array[1..n] of
        // char -- is string-like too (isCharStringType), and was missing here:
        // it type-checked into the generic TyStr fallback instead of a
        // capacity of its own, and codegen had no case for it at all, so
        // `substr(charArr, 1, 3)` link-failed on an undefined runtime symbol.
        if ((Lo == "substr" || Lo == "trim") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (isVarStringLike(ArgTy.get())) return ArgTy;
            if (!ArgTy->isError() && isCharStringType(*ArgTy))
                return Ctx_.getVarString(charStringLength(*ArgTy));
            // A bare char and the generic (unsized) string kind are
            // string-like too -- neither isVarStringLike nor isCharStringType
            // covers them, the same widening the eq/ne/lt/gt/le/ge case below
            // already makes -- and this fell through to `return TyStr` for
            // those the same as it did for a genuinely wrong argument: every
            // shape that reached here type-checked with no diagnostic at
            // all, so `substr(i, 1, 2)` for a plain integer i compiled
            // silently and reached a CodeGen path with no call to lower it
            // to (issue #261).
            if (!ArgTy->isError() && ArgTy->Kind != TypeKind::Char
                    && ArgTy->Kind != TypeKind::String)
                error(E.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ArgTy->Name});
            return TyStr;
        }
        // EP §6.7.6.7: length and index are the same string-function family
        // as substr/trim just above and eq/ne/... below, and had no argument
        // check of their own: every argument type-checked regardless, so
        // `length(i)` or `index(i, c)` for a plain integer i compiled with
        // nothing to reject them.  CodeGen's fallback for anything it does
        // not recognize as string-shaped is worse for length than the link
        // failure substr's got: it calls libc strlen on the raw integer
        // value reinterpreted as a pointer (issue #261).
        if ((Lo == "length" || Lo == "index") && !E.Args.empty()) {
            for (const auto& Arg : E.Args) {
                auto T = checkExpr(*Arg);
                if (T->isError()) continue;
                // isShortStringLike widens this for "length" only in
                // practice: "index" is EP-only (Builtins.def), so under
                // -std=turbo checkEPOnly has already refused a call to it
                // before this arm is ever reached, and under EP a
                // ShortString type does not exist for isShortStringLike to
                // ever match -- so this one extra disjunct is a genuine
                // no-op for "index" in both dialects, not a widening of what
                // "index" itself accepts.
                const bool StringLike = isVarStringLike(T.get())
                    || isCharStringType(*T) || isShortStringLike(T.get())
                    || T->Kind == TypeKind::Char || T->Kind == TypeKind::String;
                if (!StringLike)
                    error(Arg->Loc, diag::err_string_fn_arg_type, {Lo, T->Name});
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // EP §6.7.6.3: card is the cardinality of a set, so its argument must
        // be one.  This had no check at all, so `card(i)` for a plain
        // integer i type-checked with nothing to reject it: CodeGen's
        // lowering (population-count on the set's own bit-vector
        // representation) reads whatever value is there regardless, so it
        // silently population-counted i's bit pattern instead (issue #261).
        if (Lo == "card" && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (ArgTy->Kind != TypeKind::Set) {
                error(E.Args[0]->Loc, diag::err_set_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // TP-only: Assigned(p) -- p must be a pointer or a procedural value;
        // anything else has no nil to compare against.  Mirrors card's own
        // shape check just above (issue #261's own class of gap: an argument
        // that type-checked regardless reached codegen with nothing there to
        // lower it correctly).
        if (Lo == "assigned" && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyBool;
            if (ArgTy->Kind != TypeKind::Pointer && !isCallable(*ArgTy)) {
                error(E.Args[0]->Loc, diag::err_assigned_argument, {Lo, ArgTy->Name});
                return TyBool;
            }
            return TyBool;
        }
        // TP-only: SizeOf(T)/High(T)/Low(T) -- T a type name or a value
        // expression; see resolveTypeArgOrValue's own comment for the two
        // shapes this admits.
        if ((Lo == "sizeof" || Lo == "high" || Lo == "low") && !E.Args.empty()) {
            auto ArgTy = resolveTypeArgOrValue(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (ArgTy->isError()) return TyErr;
            if (Lo == "sizeof") {
                // Sema::byteSizeOf declines only for a conformant array
                // parameter type or an undiscriminated schema's extent --
                // neither reachable here today (schemas are EP-only, and a
                // conformant array TYPE has no denoting keyword or `type`
                // alias of its own to be named by), but checked rather than
                // assumed the same way byteSizeOf's every other caller does.
                if (!Sema::byteSizeOf(*ArgTy)) {
                    error(E.Args[0]->Loc, diag::err_sizeof_unknown_size, {ArgTy->Name});
                    return TyErr;
                }
                return TyInt;
            }
            // High/Low: an ordinal type/value answers directly; an array
            // type/value answers through its own INDEX type -- High(arr) is
            // the array's upper subscript, not some ordinal range the array
            // itself has none of.
            std::shared_ptr<Type> RangeTy = ArgTy;
            if (RangeTy->Kind == TypeKind::Array) RangeTy = RangeTy->IndexType;
            // Turbo open-array parameter: its bound is a RUNTIME value (this
            // activation's own synthesized bound slot -- CodeGenProcs.cpp's
            // prologue, openArrayHighBoundName's own comment), not a static
            // range ordinalRange below can ever answer about, so this is
            // checked and accepted first.  Low is always 0 and High the
            // actual's own extent minus one -- both Integer-typed, the same
            // as ordinalRange's own Integer answers elsewhere in this
            // function -- confirmed empirically against fpc -Mtp (High = -1
            // for an empty actual).
            if (RangeTy && RangeTy->Kind == TypeKind::ConformantArray
                    && RangeTy->IsOpenArray)
                return TyInt;
            if (!RangeTy || !RangeTy->isOrdinal() || !ordinalRange(*RangeTy)) {
                error(E.Args[0]->Loc, diag::err_high_low_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            // The result is a VALUE of the ranged type itself -- High(Byte)
            // is a Byte, not a bare integer -- the same "stays in the
            // argument's own type" rule succ/pred already follow above.
            return RangeTy;
        }
        // Turbo Tier 5, Cluster A item 7: TypeOf(x) -- x a type name or a
        // value expression (resolveTypeArgOrValue's own dual shape, exactly
        // like SizeOf/High/Low just above).  Confirmed against a local fpc
        // -Mtp build (typeof1.pas/typeof2.pas): the argument must be an
        // object type with at least one virtual method SOMEWHERE in its own
        // hierarchy -- VmtSlots is empty for one with none (Type::VmtSlots'
        // own comment, Type.h) -- and the result is always the generic
        // 'Pointer' type (TypeContext::getGenericPointer), the same type a
        // real 'function TypeOf(...): Pointer' declaration would answer in.
        if (Lo == "typeof" && !E.Args.empty()) {
            auto ArgTy = resolveTypeArgOrValue(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (ArgTy->isError()) return TyErr;
            if (ArgTy->Kind != TypeKind::Object || ArgTy->VmtSlots.empty()) {
                error(E.Args[0]->Loc, diag::err_typeof_argument, {ArgTy->Name});
                return TyErr;
            }
            return Ctx_.getGenericPointer();
        }
        // FPC's size-aware Hi/Lo/Swap -- a DELIBERATE divergence from
        // literal Turbo Pascal 7, whose Hi/Lo/Swap only ever worked on a
        // 16-bit value: fpc -Mtp instead sizes all three off the argument's
        // own width (Hi/Lo of a Word answer in a Byte; Hi/Lo of a LongInt
        // answer in a Word; Swap keeps the argument's own type both times),
        // silently changing behavior for any TP7 program that assumed the
        // old 16-bit-only meaning for something wider.  See CGFuncCall's
        // identical note on the codegen side.  All three need a real
        // integer at least 16 bits wide -- Byte/ShortInt (8 bits) has no
        // separate high and low half to name.
        if ((Lo == "hi" || Lo == "lo" || Lo == "swap") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (ArgTy->Kind != TypeKind::Integer || ArgTy->Width < 16) {
                error(E.Args[0]->Loc, diag::err_hi_lo_swap_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            if (Lo == "swap") return ArgTy;
            // Hi/Lo each answer in an UNSIGNED integer half the argument's
            // own width -- FPC's actual declared return types (verified
            // against fpc -Mtp's System unit: Hi/Lo(Word) -> Byte,
            // Hi/Lo(LongInt) -> Word, Hi/Lo(Int64) -> LongWord).
            return Ctx_.getInt(ArgTy->Width / 2, /*Signed=*/false);
        }
        // TP-only: Random is polymorphic on ARITY, not on its argument's
        // type the way Abs/Sqr (above) are: Random() -- no argument, this
        // arm's own `!E.Args.empty()` guard skips it -- falls through to the
        // generic `return Sym->ReturnType ...` at the very end of this Func
        // block, i.e. Builtins.def's R_Real, exactly the zero-argument
        // shape wants.  Random(Range), the one-argument form, is a
        // genuinely different result KIND (an integer, in the argument's
        // own type -- the same "stays in the argument's own type" rule
        // Hi/Lo/Swap just above and Abs/Sqr/Succ/Pred all follow) that only
        // this dedicated arm can express.
        if (Lo == "random" && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (!ArgTy->isIntegral()) {
                error(E.Args[0]->Loc, diag::err_numeric_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return ArgTy;
        }
        // EP §6.7.6.2: math functions extended to complex — return complex when
        // the argument is complex, real otherwise.
        if (!E.Args.empty() && (Lo == "sqrt" || Lo == "sin" || Lo == "cos"
                || Lo == "exp" || Lo == "ln" || Lo == "arctan")) {
            auto ArgTy = checkExpr(*E.Args[0]);
            for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
            if (ArgTy->Kind == TypeKind::Complex) return TyComplex;
            // ISO §6.6.6.2: these take an integer-type or real-type argument
            // (isNumeric() covers both, plus a subrange of either) -- this
            // was never checked, so `sqrt('a')` compiled with no diagnostic
            // at all and was always typed real (issue #261).
            if (!ArgTy->isError() && !ArgTy->isNumeric()) {
                error(E.Args[0]->Loc, diag::err_numeric_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return Sym->ReturnType ? Sym->ReturnType : TyErr; // TyReal
        }
        // EP §6.7.6.3: complex constructors -- cmplx(x,y) and polar(r,t) each
        // COMBINE two real-type components into a complex result, so both
        // arguments are numeric and, unlike the sqrt/sin/... arm above,
        // never complex (ISO 10206 §6.7.6.3's own prose: "the expressions
        // x and y [/ r and t] that shall be of real-type" -- no complex
        // alternative is offered the way Table 2's footnote (1) offers one
        // for sqrt/sin/...).  A complex argument here has no lowering
        // either: CodeGen's cmplx/polar (CGFuncCall.cpp) run each argument
        // through ToDouble, which has no case for the {double,double}
        // aggregate a complex value actually is.  Neither argument was
        // checked at all before this, so `cmplx(true, 'x')` type-checked
        // with nothing to reject it (issue #306) -- the same
        // numeric-but-not-complex shape trunc/round/int/frac already
        // require above, for the identical reason.
        if (Lo == "cmplx" || Lo == "polar") {
            for (const auto& Arg : E.Args) {
                auto ArgTy = checkExpr(*Arg);
                if (ArgTy->isError()) continue;
                if (!ArgTy->isNumeric() || ArgTy->Kind == TypeKind::Complex)
                    error(Arg->Loc, diag::err_numeric_argument, {Lo, ArgTy->Name});
            }
            return TyComplex;
        }
        // EP §6.7.6.2: component extraction functions -- unlike sqrt/sin/
        // cos/exp/ln/arctan just above, re/im/arg are NOT polymorphic over
        // integer-type and real-type too: ISO 10206 Table 2's "Type of
        // operand" column gives those six functions footnote (1),
        // "Integer-type, real-type, or complex-type", but gives re/im/arg
        // their own, narrower footnote (4), "Complex-type" alone -- the
        // standard's own mechanism for saying a required function accepts
        // more than one operand type, used deliberately for the six and
        // deliberately not for these three. So a plain numeric argument is
        // rejected here, not silently widened. This had no check at all, so
        // `re(SomeInteger)` type-checked and CodeGen's non-complex fallback
        // (a ToDouble passthrough for re, a constant 0 for im, an
        // origin-embedding plang_arg call for arg) fabricated a
        // plausible-looking but non-conforming result instead of a
        // diagnostic (issue #306).
        if ((Lo == "re" || Lo == "im" || Lo == "arg") && !E.Args.empty()) {
            auto ArgTy = checkExpr(*E.Args[0]);
            if (ArgTy->isError()) return TyErr;
            if (ArgTy->Kind != TypeKind::Complex) {
                error(E.Args[0]->Loc, diag::err_complex_argument, {Lo, ArgTy->Name});
                return TyErr;
            }
            return TyReal;
        }
        // EP §6.7.6.8: binding names the variable whose binding it reports,
        // which must be one that could have been bound.
        if (Lo == "binding") {
            checkBindingCall(Lo, E.Loc, E.Args);
            return Sym->ReturnType ? Sym->ReturnType : TyErr;
        }
        // EP §6.7.6.7: EQ/LT/GT/NE/LE/GE's two arguments "shall each be of
        // char-type or the canonical-string-type" -- the same string-or-char
        // requirement `+` and the relational operators already enforce, and
        // this had none at all: any two arguments type-checked, so a call
        // like `eq(3, 5)` was accepted here and had nothing to lower it to
        // in CodeGen (only the genuinely string-shaped case emits a call),
        // reaching the ordinary user-function path and link-failing on an
        // undefined `pas_eq`.
        if (Lo == "eq" || Lo == "ne" || Lo == "lt"
                || Lo == "gt" || Lo == "le" || Lo == "ge") {
            for (const auto& Arg : E.Args) {
                auto T = checkExpr(*Arg);
                if (T->isError()) continue;
                const bool StringLike = isVarStringLike(T.get())
                    || (!T->isError() && isCharStringType(*T))
                    || T->Kind == TypeKind::Char || T->Kind == TypeKind::String;
                if (!StringLike)
                    error(Arg->Loc, diag::err_string_fn_arg_type, {Lo, T->Name});
            }
            return TyBool;
        }
        // TP-only: the System-unit ShortString routines.  A string-like
        // argument here is ShortString, Char or a plain literal/String --
        // deliberately NOT isCharStringType (a packed array[1..n] of char):
        // Turbo's own operators (checkBinary's Plus/comparison ShortString
        // arms) never widen for that ISO/EP-only shape either, so neither do
        // these.  isTurboStringLike is local to this arm rather than a
        // Type.h predicate: nothing outside Turbo's own builtin dispatch
        // needs "string-like, Turbo's narrower sense" as a named question.
        auto isTurboStringLike = [](const std::shared_ptr<Type>& T) {
            return isShortStringLike(T.get()) || T->Kind == TypeKind::Char
                || T->Kind == TypeKind::String;
        };
        // Copy(s, index, count) -- returns a capacity-255 ShortString
        // (Builtins.def's own comment: real Turbo/FPC's declared signature is
        // `function Copy(...): string`, unrelated to any input's own
        // capacity).  index/count are checked only for being integral here;
        // out-of-range values are CLAMPED at run time (CGFuncCall.cpp), not
        // rejected at compile time or run time either -- unlike EP's substr,
        // which raises on an out-of-range request (plang_str.cpp).
        if (Lo == "copy" && E.Args.size() == 3) {
            auto ST = checkExpr(*E.Args[0]);
            auto IT = checkExpr(*E.Args[1]);
            auto CT = checkExpr(*E.Args[2]);
            if (!ST->isError() && !isTurboStringLike(ST))
                error(E.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
            if (!IT->isError() && !IT->isIntegral())
                error(E.Args[1]->Loc, diag::err_numeric_argument, {Lo, IT->Name});
            if (!CT->isError() && !CT->isIntegral())
                error(E.Args[2]->Loc, diag::err_numeric_argument, {Lo, CT->Name});
            return Ctx_.getShortString(PlangMaxStringCapacity);
        }
        // Pos(substr, s) -- 1-based index of the first match, 0 if none.
        // Builtins.def's own comment: an EMPTY pattern is 0 here (confirmed
        // against `fpc -Mtp`), the OPPOSITE of EP's index('', s) = 1.
        if (Lo == "pos" && E.Args.size() == 2) {
            auto PT = checkExpr(*E.Args[0]);
            auto ST = checkExpr(*E.Args[1]);
            if (!PT->isError() && !isTurboStringLike(PT))
                error(E.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, PT->Name});
            if (!ST->isError() && !isTurboStringLike(ST))
                error(E.Args[1]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
            return TyInt;
        }
        // Concat(s1, ..., sn) -- variadic; same capacity-255 result as Copy.
        if (Lo == "concat") {
            for (const auto& Arg : E.Args) {
                auto T = checkExpr(*Arg);
                if (!T->isError() && !isTurboStringLike(T))
                    error(Arg->Loc, diag::err_string_fn_arg_type, {Lo, T->Name});
            }
            return Ctx_.getShortString(PlangMaxStringCapacity);
        }
        // StringOfChar(ch, count) -- count copies of ch, capacity-255 result.
        if (Lo == "stringofchar" && E.Args.size() == 2) {
            auto ChT = checkExpr(*E.Args[0]);
            auto CT  = checkExpr(*E.Args[1]);
            if (!ChT->isError() && ChT->Kind != TypeKind::Char)
                error(E.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ChT->Name});
            if (!CT->isError() && !CT->isIntegral())
                error(E.Args[1]->Loc, diag::err_numeric_argument, {Lo, CT->Name});
            return Ctx_.getShortString(PlangMaxStringCapacity);
        }
        // UpCase(ch): Char -- real Turbo Pascal 7's own single-character
        // form; see Builtins.def's own comment on why the later Delphi
        // string-argument overload is out of scope.
        if (Lo == "upcase" && !E.Args.empty()) {
            auto ChT = checkExpr(*E.Args[0]);
            if (!ChT->isError() && ChT->Kind != TypeKind::Char)
                error(E.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ChT->Name});
            return TyChar;
        }
        // ParamCount: Integer -- the number of command-line arguments, not
        // counting argv[0] itself (Builtins.def's own comment).  No
        // arguments to check.
        if (Lo == "paramcount") {
            return TyInt;
        }
        // ParamStr(n): a capacity-255 ShortString -- argv[n], or '' for n
        // outside 0..ParamCount (runtime/plang_sys.cpp's plang_tp_paramstr's
        // own comment: confirmed against `fpc -Mtp` that an out-of-range
        // index is not an error).
        if (Lo == "paramstr" && !E.Args.empty()) {
            auto NT = checkExpr(*E.Args[0]);
            if (!NT->isError() && !NT->isIntegral())
                error(E.Args[0]->Loc, diag::err_numeric_argument, {Lo, NT->Name});
            return Ctx_.getShortString(PlangMaxStringCapacity);
        }
        for (const auto& Arg : E.Args) (void)checkExpr(*Arg);
        return Sym->ReturnType ? Sym->ReturnType : TyErr;
    }
    // Turbo procedural VALUES: checkUserDefinedCall's own Var-kind arm reads
    // this as an indirect call, but Sym here is 'f' itself -- Phase 7's
    // unused-variable audit (Sema.cpp) only ever looks at SymbolKind::Var,
    // so without this, calling f and never otherwise reading it warned f
    // unused despite the call being exactly a use of it.
    if (Sym->Kind == SymbolKind::Var) Sym->Referenced = true;
    return checkUserDefinedCall(*Sym, E.Loc, E.Args, /*expectFunction=*/true);
}

std::shared_ptr<Type> Sema::checkTypeCast(const TypeCastExpr& E) {
    // Resolved exactly as an ordinary type-denoter naming this same spelling
    // would be -- a synthetic NamedTypeNode reaches every existing rule
    // (built-in keyword, user TypeAlias, EP schema, 'string' gated to EP)
    // with no rule of its own duplicated here. resolveSchemaParams
    // (SemaType.cpp) already uses this exact idiom for the same reason.
    NamedTypeNode TargetNode;
    TargetNode.Loc  = E.Loc;
    TargetNode.Name = E.TypeName;
    auto TargetTy = resolveNamed(TargetNode);

    // Turbo untyped parameter (`procedure P(var x)`): a variable typecast is
    // the classic memcpy/memcmp idiom's whole reason to exist
    // (`FillChar(TByteArray(x), N, 0)`) and the one context checkIdent's own
    // "used bare" diagnostic must NOT fire in -- checked directly here,
    // before the ordinary checkExpr(*E.Operand) below would otherwise reach
    // checkIdent and reject it.  There is no source size to require a match
    // with (x could be anything at all), so this bypasses BothScalar/
    // SameSize below entirely and simply accepts TargetTy -- E.Operand's own
    // ResolvedType is set to TargetTy too, so isLValue's identical
    // TypeCastExpr case (which reads it back as Src) computes a trivial
    // same-size match rather than needing its own copy of this special case.
    if (auto* Id = llvm::dyn_cast<IdentExpr>(E.Operand.get())) {
        if (Symbol* Sym = Symtab.lookup(Id->Name);
                Sym && !Sym->Ty
                && (Sym->Kind == SymbolKind::Var || Sym->Kind == SymbolKind::VarParam)) {
            Sym->Referenced = true;
            E.Operand->ResolvedType = TargetTy;
            if (TargetTy->isError()) return TyErr;
            return TargetTy;
        }
    }

    auto SrcTy = checkExpr(*E.Operand);

    if (TargetTy->isError() || SrcTy->isError()) return TyErr;

    // Turbo/EP's own conversion rules: real<->ordinal truncates/rounds like
    // Trunc/Round would, and ordinal<->ordinal reinterprets the ordinal
    // value (Integer(SomeChar) reads SomeChar's ordinal position as an
    // Integer). Defined for every ordinal-or-real pair regardless of size.
    const bool BothScalar = (TargetTy->isOrdinal() || TargetTy->Kind == TypeKind::Real)
                          && (SrcTy->isOrdinal()    || SrcTy->Kind == TypeKind::Real);

    // A VARIABLE typecast reinterprets the operand's own storage bit-for-bit
    // in place, which is only meaningful when the two types occupy the same
    // number of bytes -- TByteRec(SomeWord) requires TByteRec to be exactly
    // 2 bytes, the same as Word.  Meaningful for any two types, scalar or
    // not (TByteRec is a record, neither ordinal nor real).  Whether THIS
    // particular occurrence can actually be used as a variable additionally
    // needs the operand to be an lvalue in the first place -- isLValue's own
    // TypeCastExpr case checks that; this only asks "could reinterpreting
    // these two types' storage ever make sense."
    const auto TargetSz = byteSizeOf(*TargetTy);
    const auto SrcSz     = byteSizeOf(*SrcTy);
    const bool SameSize  = TargetSz && SrcSz && *TargetSz == *SrcSz;

    if (!BothScalar && !SameSize) {
        error(E.Loc, diag::err_invalid_type_cast, {SrcTy->Name, TargetTy->Name});
        return TyErr;
    }
    return TargetTy;
}

std::shared_ptr<Type> Sema::checkSetLit(const SetLiteralExpr& E, const std::shared_ptr<Type>& TargetHint) {
    if (E.Elements.empty()) {
        // Empty set: type is indeterminate; use a generic set-of-integer.
        auto T = std::make_shared<Type>();
        T->Kind     = TypeKind::Set;
        T->Name     = "[]";
        T->ElemType = TyInt;
        return T;
    }

    // EP §6.8.7: a TYPED set constructor names its type, and ISO §6.7.1 requires
    // its members to be of that type's base type.  This ignored E.TypeName
    // outright and derived the type from the ELEMENTS, so `cs['x', 300]` for a
    // `set of col` was accepted and produced the empty set -- while the untyped
    // `['x']` in the same context IS caught, so the two spellings of one
    // construct disagreed about whether the program was legal.
    std::shared_ptr<Type> Named;
    if (!E.TypeName.empty())
        if (const Symbol* Sym = Symtab.lookup(E.TypeName))
            if (Sym->Kind == SymbolKind::TypeAlias && Sym->Ty
                    && Sym->Ty->Kind == TypeKind::Set)
                Named = Sym->Ty;

    std::shared_ptr<Type> BaseType;
    for (const auto& Elem : E.Elements) {
        std::shared_ptr<Type> Et;
        if (auto* Rng = llvm::dyn_cast<SetRangeExpr>(Elem.get())) {
            auto Lo = checkExpr(*Rng->Low);
            auto Hi = checkExpr(*Rng->High);
            // ISO §6.7.1's member-range is the same `lo..hi` range ISO
            // §6.4.2.2 covers for a subrange-type: both bounds must be
            // constants of the SAME ordinal type.  Picking "whichever bound
            // is ordinal" below, same as resolveTypeImpl did before issue
            // #251, never asked whether the OTHER bound agreed, so
            // `[1..'z']` (integer, char; each ordinal on its own) was
            // silently accepted as a set of integer (issue #395).
            if (!boundsShareOrdinalType(*Lo, *Rng->High, *Hi)) Et = TyErr;
            else Et = Lo->isOrdinal() ? Lo : Hi;
        } else {
            Et = checkExpr(*Elem);
        }
        if (!Et->isError()) {
            if (!Et->isOrdinal()) {
                error(Elem->Loc, diag::err_set_elem_not_ordinal, {Et->Name});
            }
            if (Named && Named->ElemType && !Named->ElemType->isError()
                    && !isAssignCompatible(*Named->ElemType, *Et))
                error(Elem->Loc, diag::err_assign_mismatch,
                      {Et->Name, Named->ElemType->Name});
            if (!BaseType) BaseType = Et;
        }
    }
    // A named constructor IS that type, whatever its elements happened to be.
    if (Named) {
        if (Named->ElemType) warnIfSetLitOutOfRange(*Named->ElemType, E);
        return Named;
    }
    // A caller-supplied target (checkAssignStmt, for a literal with no
    // E.TypeName of its own) suppresses the element-count check below the
    // same way Named does: that check exists only because an unadopted
    // literal's runtime representation has nothing but its own raw values to
    // size a bitmask from, and once a real bounded target is known, that
    // concern doesn't apply. Unlike Named, this doesn't also call
    // warnIfSetLitOutOfRange itself -- the caller (checkAssignStmt) already
    // does that against Dst after this returns, and doing it here too would
    // warn twice for one out-of-range element.
    if (TargetHint) return TargetHint;

    auto T = std::make_shared<Type>();
    T->Kind     = TypeKind::Set;
    T->Name     = "set literal";
    T->ElemType = BaseType ? BaseType : TyInt;
    // With no context to name a set type, `card([-1, 3])` still has to put
    // ordinal -1 somewhere, so read a window off the elements themselves. A
    // context that does name a type replaces this wholesale via adoptSetType.
    //
    // The same window also has to be checked against PlangMaxSetElements the
    // way a named `set of` base type is in checkSetBaseRange: a literal like
    // `[0, 300]` never gets a subrange window at all under the old rule below
    // (nothing here is negative), so nothing ever caught it spanning more
    // than a set can represent, and codegen's bitmask silently dropped 300.
    if (T->ElemType && T->ElemType->Kind == TypeKind::Integer) {
        if (auto Window = literalSetWindow(E)) {
            // A negative low bound shifts the window rather than widening it,
            // same as setBaseOffset for a named base type.
            const int64_t Offset = Window->first < 0 ? Window->first : 0;
            if (Window->second - Offset >= PlangMaxSetElements) {
                error(E.Loc, diag::err_set_lit_too_wide,
                      {std::to_string(Window->first), std::to_string(Window->second),
                       std::to_string(PlangMaxSetElements)});
            } else if (Window->first < 0) {
                T->ElemType = Ctx_.getSubrange(TyInt, Window->first, Window->second);
            }
        }
    } else if (T->ElemType && !T->ElemType->isError()) {
        // Issue #404: a non-Integer ordinal element type (an Enum, most
        // plausibly, but also e.g. a Subrange picked up from a variable's own
        // type) has a fixed width of its own that does not depend on folding
        // the literal's elements the way literalSetWindow does for Integer --
        // it is exactly the same check checkSetBaseRange makes for a named
        // `set of Base` (SemaType.cpp) and the one issue #227 added to the
        // `x in [...]` adoption path above.  This, the OLDER untyped fallback
        // that predates #227, never made it at all: `card([e0, e299])` for a
        // >256-value enum silently dropped e299 past bit 255 in the runtime
        // bitmask instead of being diagnosed.
        checkSetBaseRange(*T->ElemType, E.Loc);
    }
    return T;
}

// §6.4.6, §6.7.2.4: a set's members lie in its base type, and a member that
// is a compile-time constant is checked now the same way
// warnIfConstantOutOfRange checks one assigned to a scalar subrange variable
// -- assignment-compatibility alone (checked above, per element) accepts any
// integer literal for a `set of 1..10`, constant 999 included, so the value
// itself still has to be checked against ElemBase's own range.
void Sema::warnIfSetLitOutOfRange(const Type& ElemBase, const ExprNode& E) {
    if (auto* B = llvm::dyn_cast<BinaryExpr>(&E)) {
        warnIfSetLitOutOfRange(ElemBase, *B->Left);
        warnIfSetLitOutOfRange(ElemBase, *B->Right);
        return;
    }
    auto* SL = llvm::dyn_cast<SetLiteralExpr>(&E);
    if (!SL) return;
    // A typed constructor (`cs[999]`) already had this same check made
    // against its OWN named type, from inside checkSetLit -- against
    // ElemBase here too would warn twice for one `999`, since isLooseSet
    // (and so adoptSetType, and the caller in checkAssignStmt that also
    // calls this) does not distinguish a typed set-literal from an untyped
    // one.
    if (!SL->TypeName.empty()) return;
    auto Range = ordinalRange(ElemBase);
    if (!Range) return;
    auto warnIfOOR = [&](const ExprNode& X) {
        auto V = constBound(X);
        if (!V || (*V >= Range->first && *V <= Range->second)) return;
        warning(X.Loc, diag::warn_const_out_of_range,
                {spellOrdinal(ElemBase, *V), spellOrdinal(ElemBase, Range->first),
                 spellOrdinal(ElemBase, Range->second)});
    };
    for (const auto& Elem : SL->Elements) {
        if (auto* Rng = llvm::dyn_cast<SetRangeExpr>(Elem.get())) {
            warnIfOOR(*Rng->Low);
            warnIfOOR(*Rng->High);
        } else {
            warnIfOOR(*Elem);
        }
    }
}

/// The span of a set-constructor's ordinals, when every one of them folds.
/// Nothing otherwise: an element that doesn't fold (a variable, say) leaves
/// no compile-time window to derive, the same as an unfoldable named-type
/// bound in checkSetBaseRange -- codegen clamps such sets at run time
/// instead.
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
    if (!Any) return std::nullopt;
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
                for (const auto& lbl : arm.Labels) {
                    auto lblTy = checkExpr(*lbl);
                    // EP §6.8.7.2: the labels SELECT components, so one outside
                    // the index type selects nothing and its value is dropped.
                    // `arr[1:1; 2:2; 5:555; -1:888]` for an array[1..4]
                    // compiled clean and emitted four stores; the other four
                    // component values vanished without a word.
                    if (!T->IndexType || T->IndexType->isError()) continue;
                    if (!lblTy->isError()
                            && !isAssignCompatible(*T->IndexType, *lblTy)) {
                        error(lbl->Loc, diag::err_assign_mismatch,
                              {lblTy->Name, T->IndexType->Name});
                        continue;
                    }
                    if (auto V = constBound(*lbl))
                        if (*V < T->IndexType->SubLo || *V > T->IndexType->SubHi)
                            error(lbl->Loc,
                                  diag::err_constructor_label_out_of_range,
                                  {std::to_string(*V), T->IndexType->Name});
                }
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
                auto valTy = checkExpr(*arm.Value);
                ExpectedValueType_ = nullptr;
                // The result was DISCARDED here, so a component value of any
                // type at all was accepted and then stored at the field's
                // address: `outer[n: iv; m: 9]` with `iv` a 64-byte record and
                // `n` an integer emitted a 64-byte store into an 8-byte field
                // and took the stack with it.  The array arm above has always
                // asked this question.
                if (Named && Named->Ty && !Named->Ty->isError()
                        && !valTy->isError()
                        && !isAssignCompatible(*Named->Ty, *valTy))
                    error(arm.Value->Loc, diag::err_assign_mismatch,
                          {valTy->Name, Named->Ty->Name});
                if (Named) adoptSetType(*arm.Value, Named->Ty);
            }
        }
        return T;
    }

    if (T->Kind == TypeKind::Set) {
        // Typed set constructor — each arm's labels are set elements, and
        // ISO §6.7.1 requires them to be of the set's base type.  Nothing asked,
        // so `cs['x', 300]` for a `set of col` compiled and produced the empty
        // set -- the untyped form `['x']` in the same context IS caught, so the
        // two spellings of one construct disagreed about whether it was legal.
        for (const auto& arm : E.Arms) {
            for (const auto& lbl : arm.Labels) {
                auto lblTy = checkExpr(*lbl);
                if (T->ElemType && !T->ElemType->isError() && !lblTy->isError()
                        && !isAssignCompatible(*T->ElemType, *lblTy))
                    error(lbl->Loc, diag::err_assign_mismatch,
                          {lblTy->Name, T->ElemType->Name});
            }
            if (arm.Value) (void)checkExpr(*arm.Value);
        }
        return T;
    }

    error(E.Loc, diag::err_constructor_not_aggregate, {T->Name});
    checkAllArms();
    return TyErr;
}

// ---------------------------------------------------------------------------
// TP-only: typed-constant initializer foldability (see this method's own
// declaration, Sema.h, for the overall design)
// ---------------------------------------------------------------------------

void Sema::checkTypedConstFoldable(const ExprNode& E, const std::string& Name) {
    if (auto* SV = llvm::dyn_cast<StructuredValueExpr>(&E)) {
        // Turbo's own array literal is purely positional -- '(1, 2, 3)', no
        // EP-style index label -- so an all-unlabeled constructor over an
        // array whose extent folds is checked for supplying exactly that
        // many elements.  A labeled or partially-labeled constructor is not
        // Turbo's own literal form (nothing parseTurboConstValue builds looks
        // like that); left alone here, the way checkStructuredValue already
        // checked it stands.
        if (SV->ResolvedType && SV->ResolvedType->Kind == TypeKind::Array
                && SV->ResolvedType->IndexType
                && !SV->ResolvedType->IndexType->isError()) {
            bool AllPositional = true;
            for (const auto& Arm : SV->Arms)
                if (!Arm.Labels.empty() || Arm.IsOtherwise) { AllPositional = false; break; }
            if (AllPositional) {
                const int64_t Lo = SV->ResolvedType->IndexType->SubLo;
                const int64_t Hi = SV->ResolvedType->IndexType->SubHi;
                if (Hi >= Lo) {
                    const uint64_t Count =
                        static_cast<uint64_t>(Hi) - static_cast<uint64_t>(Lo) + 1;
                    if (SV->Arms.size() != Count)
                        error(SV->Loc, diag::err_typed_const_array_count_mismatch,
                              {Name, std::to_string(SV->Arms.size()),
                               std::to_string(Count)});
                }
            }
        }
        for (const auto& Arm : SV->Arms)
            if (Arm.Value) checkTypedConstFoldable(*Arm.Value, Name);
        return;
    }
    if (constBound(E)) return;
    if (constRealBound(E)) return;
    error(E.Loc, diag::err_typed_const_not_constant, {Name});
}

// ---------------------------------------------------------------------------
// Call argument checking
// ---------------------------------------------------------------------------

// See NumSemaTypeKinds in Sema/Type.h.  A new structured kind that can hold a
// component defaults to "contains no file", and ISO §6.6.3.3's rule that a
// file may not be passed by value stops being enforced through it.
static_assert(NumSemaTypeKinds == 23,
              "a new structured type kind needs a case in typeContainsFile");

bool Sema::typeContainsFile(const Type& T) {
    switch (T.Kind) {
    case TypeKind::File:   return true;
    // Turbo Tier 5: an Object's own RecordFields is the flattened
    // ancestor-then-own field list (Type::RecordFields's own comment,
    // Type.h), so walking it here already covers a file field inherited
    // from an ancestor, not just one this type declares itself.
    case TypeKind::Record:
    case TypeKind::Object:
        for (const auto& F : T.RecordFields)
            if (F.Ty && typeContainsFile(*F.Ty)) return true;
        return false;
    // An array of files holds files as surely as a record of them does, and
    // was the way round the rule.  A conformant array (EP §6.7.3.7) is the
    // same rule with its element type carried in the same field.
    case TypeKind::Array:
    case TypeKind::Set:
    case TypeKind::ConformantArray:
        return T.ElemType && typeContainsFile(*T.ElemType);
    // EP §6.4.7: a schema's body is itself a record or array assembled from
    // these same building blocks (see SchemaBody), so a file anywhere in it
    // is exactly as forbidden as one in an ordinary field or element.
    case TypeKind::SchemaInstance:
    case TypeKind::Schema:
        return T.SchemaBody && typeContainsFile(*T.SchemaBody);
    default:               return false;
    }
}

namespace {

// EP §6.4.7: Returns true if two SchemaInstance types represent the same
// instantiation of the same schema DECLARATION.
//
// A schema is identified by its declaration, not by its spelling -- see
// isAssignCompatible's SchemaInstance arm (c03cd04) for the story: two
// `vec(3)` from unconnected declarations are two different types even though
// they print alike and share every discriminant value.  That fix taught
// assignment compatibility the rule but left this function, which backs
// var-parameter identity (ISO §6.6.3.3) and forward-declaration congruity
// (ISO §6.6.3.6), still comparing spellings -- so a `var` formal happily
// aliased an unrelated same-named schema instance across a scope boundary.
//
// Unlike isAssignCompatible there is no falling back to comparing bodies
// when both declarations are known and differ: identity, not mere structural
// resemblance, is what a var parameter and a re-declared heading require.
bool schemaInstMatch(const Type& A, const Type& B) {
    if (A.Kind != TypeKind::SchemaInstance || B.Kind != TypeKind::SchemaInstance)
        return false;
    // Where a declaration is unknown on either side -- separate compilation
    // gives the same schema a different node in each unit -- the name and
    // discriminants are all that is left to compare.
    if (A.SchemaBodyNode && B.SchemaBodyNode && A.SchemaBodyNode != B.SchemaBodyNode)
        return false;
    // Pascal identifiers are case-insensitive; see e.g. sameParamType's
    // undiscriminated-Schema arm and isLValue's discriminant check below.
    if (!eqCI(A.SchemaName, B.SchemaName)) return false;
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
    // Turbo procedural VALUES: 'f(...)' where f names a procedural VARIABLE
    // (an ordinary Var, not a declared routine -- SymbolKind::Proc is a
    // DECLARATION, and f is not one) is an indirect call through whatever
    // routine f currently holds.  Sym itself carries none of
    // IsFunction/Params/ReturnType -- those live on Sym.Ty, the Procedure/
    // Function Type SemaType.cpp's ProcedureTypeNode arm built when f's
    // procedural type was resolved -- so they are borrowed into a
    // routine-shaped stand-in and this same function is asked again with
    // it, once, so every check below (arity, argument congruity, the
    // {$X+}/ExpectFunction discard rule) runs exactly as it does for a
    // genuinely declared routine, with no second copy of any of it.
    if (Sym.Kind == SymbolKind::Var && Sym.Ty && isCallable(*Sym.Ty)) {
        Symbol Indirect;
        Indirect.Kind       = SymbolKind::Proc;
        Indirect.Name       = Sym.Name;
        Indirect.IsFunction = Sym.Ty->Kind == TypeKind::Function;
        Indirect.Params     = Sym.Ty->Params;
        Indirect.ReturnType = Sym.Ty->RetType;
        return checkUserDefinedCall(Indirect, CallLoc, Args, ExpectFunction);
    }
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
    if (!ExpectFunction && Sym.IsFunction
            && !(Opts.turbo() && Opts.switchOn(Switch::ExtendedSyntax, CallLoc))) {
        // ISO §6.8.2.2 requires a function's result be used.  Turbo's
        // `{$X+}` (its default -- CompilerSwitches.def's TurboDefault
        // column) lifts that requirement and lets the result be discarded
        // like an ordinary procedure call's absence of one; `{$X-}` puts
        // ISO 7185/Extended Pascal's own rule back in force.
        //
        // Opts.turbo() is not redundant with switchOn: SwitchTable's default
        // answers "extended syntax allowed" for every dialect, ISO 7185 and
        // Extended Pascal included, and those two have no `{$X}` directive
        // to ever say otherwise -- the same gap CGBinaryOps::emitBinary's
        // isTurbo()-guarded boolEvalAt already works around for `{$B}`.
        error(CallLoc, diag::err_func_as_statement, {Sym.Name});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }
    checkCallArgs(Sym, CallLoc, Args);
    return Sym.ReturnType ? Sym.ReturnType : TyErr;
}

// Turbo Tier 5, Cluster A item 3: see this function's own declaration
// (Sema.h) for the design.  Shared by checkMethodCallExpr (ExpectFunction =
// true) and checkMethodCallStmt (ExpectFunction = false, defined in
// SemaStmt.cpp) -- both just check the receiver/method, then hand off.
std::shared_ptr<Type> Sema::checkMethodCall(
        const ExprNode& Receiver, const std::string& Method, SourceLocation Loc,
        std::span<const std::unique_ptr<ExprNode>> Args, bool ExpectFunction) {
    auto RecvTy = checkExpr(Receiver);
    if (RecvTy->isError()) {
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }
    // Confirmed against a local fpc -Mtp build: a pointer receiver must be
    // explicitly dereferenced ('P^.Method', never 'P.Method' -- "Illegal
    // qualifier") -- so by the time a genuine method call reaches here, the
    // '^' has already been parsed as a DerefExpr and checkExpr(Receiver) has
    // already unwrapped it to the pointee's own Object type.  RecvTy is
    // therefore checked directly against TypeKind::Object, with no separate
    // TypeKind::Pointer branch: a bare pointer receiver correctly falls into
    // the "not an object" diagnostic below, matching fpc's own rejection.
    if (RecvTy->Kind != TypeKind::Object) {
        error(Loc, diag::err_method_call_receiver_not_object, {RecvTy->Name});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }

    // Ancestor-chain walk for a method named Method: the same MRO order
    // resolveObjectType's own VmtSlots inheritance uses (SemaType.cpp), just
    // expressed through the public composite-key symbol lookup
    // (Sema::objectMethodKey) that resolveObjectType itself registered each
    // TYPE-LEVEL method under, rather than SemaType.cpp's file-local
    // findMethodInChain helper (which walks Type::ObjectMethods directly and
    // is not visible outside that translation unit) -- both walk Parent the
    // same way and would find the same declaration.
    const Symbol* MethodSym = nullptr;
    for (const Type* Cur = RecvTy.get(); Cur; Cur = Cur->Parent.get()) {
        Symbol* S = Symtab.lookup(objectMethodKey(Cur->Name, Method));
        if (S && S->Kind == SymbolKind::Method) { MethodSym = S; break; }
    }
    if (!MethodSym) {
        error(Loc, diag::err_object_method_not_found, {RecvTy->Name, Method});
        for (const auto& A : Args) (void)checkExpr(*A);
        return TyErr;
    }

    // Turbo Tier 5, Cluster A item 7: private-method visibility -- see
    // err_object_private_field/err_object_private_method's own comment
    // (DiagnosticSemaKinds.def) for the confirmed real (whole-module) scope.
    // MethodSym->Module already carries "where declared" for a Method
    // symbol via the same generic unit-export stamping every other symbol
    // kind gets (Sema.cpp/SemaType.cpp's forEachInCurrentScope loops).
    if (MethodSym->IsMethodPrivate && MethodSym->Module != CurrentUnit_)
        error(Loc, diag::err_object_private_method,
              {MethodSym->MethodOwnerType, Method});

    // Hand off to the SAME arity/argument-type checking an ordinary
    // procedure/function call gets, via a synthetic SymbolKind::Proc
    // stand-in -- the identical trick checkUserDefinedCall's own Var/
    // procedural-value arm already uses just above, so this is not a second
    // implementation of argument checking.
    Symbol Indirect;
    Indirect.Kind       = SymbolKind::Proc;
    Indirect.Name       = MethodSym->MethodOwnerType + "." +
                           (MethodSym->Decl ? MethodSym->Decl->Name : Method);
    Indirect.IsFunction = MethodSym->IsFunction;
    Indirect.Params     = MethodSym->Params;
    Indirect.ReturnType = MethodSym->ReturnType;
    return checkUserDefinedCall(Indirect, Loc, Args, ExpectFunction);
}

std::shared_ptr<Type> Sema::checkMethodCallExpr(const MethodCallExpr& E) {
    return checkMethodCall(*E.Receiver, E.Method, E.Loc, E.Args, /*ExpectFunction=*/true);
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
    // A fixed-discriminant schema instance (e.g. Vec(5)) is written out afresh
    // at every occurrence -- a forward declaration's parameter and its
    // matching definition's parameter are two distinct Type objects even when
    // they denote the same instantiation -- so pointer identity would call a
    // schema instance incongruous with itself.  Compare structurally instead:
    // same schema name and identical (constant-folded) discriminant values.
    if (A->Kind == TypeKind::SchemaInstance)
        return schemaInstMatch(*A, *B);
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
        // Turbo untyped parameter: Ty is deliberately null on both sides of
        // a congruous pair (`procedure(var x)` congruous with
        // `procedure(var y)`), and sameParamType(nullptr, nullptr) answers
        // false -- it has no way to tell that apart from a genuine
        // resolution failure on one or both sides, which IS meant to stay
        // incongruous. IsUntyped disambiguates the two: an untyped formal is
        // congruous only with another untyped one, nothing else.
        if (A.Params[I].IsUntyped != B.Params[I].IsUntyped) return false;
        if (A.Params[I].IsUntyped) continue;
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
        // If the name is a required identifier that the active dialect
        // doesn't have at all, that is the more useful diagnostic to give —
        // the same one the direct-call path (checkCallExpr) already gives —
        // rather than telling the user it "cannot be passed as a parameter",
        // which reads as though the name exists here but the wrong way.
        if (!checkEPOnly(*S, Arg.Loc)) return;
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

// ---------------------------------------------------------------------------
// Turbo procedural TYPES and VALUES
// ---------------------------------------------------------------------------

bool Sema::isRoutineNameCandidate(const IdentExpr& Id) const {
    const Symbol* S = Symtab.lookup(Id.Name);
    return S && S->Kind == SymbolKind::Proc;
}

std::shared_ptr<Type> Sema::checkRoutineValue(const IdentExpr& Id) {
    // Callers only reach here once isRoutineNameCandidate (or an equivalent
    // direct lookup) has already found a SymbolKind::Proc under this name;
    // re-looked-up rather than passed in so this function is self-contained
    // and every call site reads the same way.
    Symbol* Sym = Symtab.lookup(Id.Name);
    if (!Sym || Sym->Kind != SymbolKind::Proc) {
        // Not reachable through either call site below, but a defensive
        // answer costs nothing and keeps this function total.
        error(Id.Loc, diag::err_undefined_identifier, {Id.Name});
        return TyErr;
    }
    Sym->Referenced = true;

    // A procedural PARAMETER is itself a {entry point, frame} pair received
    // at run time, and what it is bound to -- possibly a nested, capturing
    // routine from some other activation entirely -- is not knowable here.
    // Storing just its entry point into a procedural variable's flat pointer
    // would silently drop that frame, so this is refused outright rather
    // than only for a parameter PROVEN to be bound to something that
    // captures -- symmetrical with the nested-routine refusal just below,
    // and for the same reason (err on the side of rejecting more).
    if (Sym->IsProcParam) {
        error(Id.Loc, diag::err_procval_of_proc_param, {Id.Name});
        Id.Resolution = IdentExpr::IdentResolution::RoutineReference;
        return TyErr;
    }
    // See Symbol::IsNested's own comment: a nested routine may read/write its
    // enclosing activation's variables through a static link that a
    // procedural variable -- one flat pointer, no frame slot -- cannot
    // carry.  Assigning one in here would compile cleanly and dangle the
    // moment the defining activation returns; real Turbo Pascal disallows
    // this outright, and so does plang.
    if (Sym->IsNested) {
        error(Id.Loc, diag::err_procval_of_nested_routine, {Id.Name});
        Id.Resolution = IdentExpr::IdentResolution::RoutineReference;
        return TyErr;
    }

    Id.Resolution = IdentExpr::IdentResolution::RoutineReference;
    auto T  = std::make_shared<Type>();
    T->Kind = Sym->IsFunction ? TypeKind::Function : TypeKind::Procedure;
    T->Params  = Sym->Params;
    T->RetType = Sym->ReturnType;
    T->Name    = describeCallable(*T);
    return T;
}

/// The ordinal index type of \p T's outermost dimension, for an actual that
/// may conform to a conformant-array schema: a plain Array's IndexType, or
/// (when relaying an already-conformant parameter to another one) a
/// ConformantArray's own first bound's declared type.  Null for anything
/// else, or when that dimension's type never resolved.  Shared by
/// isConformable and its caller's diagnostic, so the two cannot disagree on
/// what "the actual's index type" means.
static std::shared_ptr<Type> outerIndexTypeOf(const Type& T) {
    if (T.Kind == TypeKind::Array) return T.IndexType;
    if (T.Kind == TypeKind::ConformantArray && !T.ConformantBounds.empty())
        return T.ConformantBounds[0].OrdType;
    return nullptr;
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

        // Turbo untyped parameter (`procedure P(var x)`): Param.Ty is
        // deliberately null (see ParamGroup::Type's own comment) and none of
        // the Param.Ty-dereferencing branches below can run at all -- this
        // handles the whole match on its own, before any of them, and
        // `continue`s so none of them see a null Param.Ty.
        //
        // An untyped formal accepts ANY addressable variable (its own
        // declared type is not otherwise examined -- the callee will
        // typecast it) -- checked the same way FillChar/Move's own
        // "untyped" first arguments already are (SemaStmt.cpp) -- with one
        // extra allowance real Turbo Pascal has that those built-ins don't
        // need: relaying an untyped parameter THIS activation already has
        // straight through to another untyped formal, with no typecast,
        // confirmed against fpc -Mtp (and confirmed, also against fpc,
        // that relaying one to a TYPED var formal instead is rejected --
        // so that case is deliberately left to fall through to the ordinary
        // checkExpr/checkIdent path below, which reports it).
        if (Param.IsUntyped) {
            bool RelayedUntyped = false;
            if (auto* Id = llvm::dyn_cast<IdentExpr>(&ArgNode)) {
                if (Symbol* ASym = Symtab.lookup(Id->Name);
                        ASym && !ASym->Ty
                        && (ASym->Kind == SymbolKind::Var
                            || ASym->Kind == SymbolKind::VarParam)) {
                    ASym->Referenced = true;
                    RelayedUntyped = true;
                }
            }
            if (!RelayedUntyped) {
                auto AtUntyped = checkExpr(ArgNode);
                checkNotProtected(ArgNode, ArgNode.Loc);
                if (!AtUntyped->isError() && !isLValue(ArgNode)) {
                    auto IdxStr = std::to_string(Idx + 1);
                    error(ArgNode.Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view(IdxStr), std::string_view(Sym.Name)});
                }
            }
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
            // A var-parameter actual is written to by the callee as surely
            // as an assignment writes to it, so a protected one may not be
            // passed.  EP §6.7.3.1.
            if (Param.IsVar) checkNotProtected(ArgNode, ArgNode.Loc);
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
            // EP §6.4.7: a schema whose body is an array IS an array for this
            // purpose -- `p^` for a `^vec` conforms to the same formals a
            // declared array does, and refusing it made the one way to write a
            // procedure over an undiscriminated schema unavailable.  The body
            // may itself be another schema instantiation, so the question is
            // asked of what it underlies to -- `B(n) = A(n)` for an array `A`
            // is an array too, one level further down.
            const Type* Actual = At.get();
            if (Actual->Kind == TypeKind::Schema
                    || Actual->Kind == TypeKind::SchemaInstance) {
                const Type* Underlying = schemaUnderlying(Actual);
                if (Underlying->Kind == TypeKind::Array)
                    Actual = Underlying;
            }
            if (!isConformable(*Param.Ty, *Actual)) {
                // ISO §6.7.3.8: report whichever of a)/d)/the element type
                // actually failed, in the same order (and at the same nesting
                // level) isConformable itself checks them -- otherwise a
                // packedness or index-type mismatch was reported as an
                // element-type mismatch with the SAME type named on both
                // sides ("expected 'integer', got 'integer'"), which only
                // isConformable's old element-only check could never
                // produce and this one now can.  Issue #406: this used to
                // inspect only the OUTERMOST dimension itself instead of
                // delegating to a helper that recurses the same way
                // isConformable does, so a multi-dimensional schema whose
                // mismatch was in an INNER dimension still fell through to
                // the generic element-type message even though isConformable
                // (which does recurse) was the very thing that rejected it.
                diagnoseConformMismatch(Param.Name, ArgNode.Loc, *Param.Ty, *Actual);
            }
            // Conformant var params still require an lvalue.
            // A var-parameter actual is written to by the callee as surely
            // as an assignment writes to it, so a protected one may not be
            // passed.  EP §6.7.3.1.
            if (Param.IsVar) checkNotProtected(ArgNode, ArgNode.Loc);
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
            // A var-parameter actual is written to by the callee as surely as
            // an assignment writes to it, so a protected one may not be passed.
            // EP §6.7.3.1.
            checkNotProtected(ArgNode, ArgNode.Loc);
            // A pointer-to-schema-instance formal (e.g. `var x: VecPtrA`)
            // is itself Pointer-kind, not SchemaInstance, so the
            // schemaInstMatch disjunct just above never fires for it --
            // it only ever sees the two Pointer objects, never their
            // pointees.  Two independently-declared aliases of `^Vec(4)`
            // are two distinct Pointer objects (their pointees are two
            // distinct, un-interned SchemaInstance objects; see
            // schemaInstMatch's comment), so isIdenticalType(At, Param.Ty)
            // fails too, and a var parameter typed with one alias wrongly
            // refused an actual of the other.  Issue #407.
            bool typeOk = isIdenticalType(At, Param.Ty)
                       || (At && Param.Ty && schemaInstMatch(*At, *Param.Ty))
                       || (At && Param.Ty
                           && At->Kind == TypeKind::Pointer
                           && Param.Ty->Kind == TypeKind::Pointer
                           && At->PointeeType && Param.Ty->PointeeType
                           && (isIdenticalType(At->PointeeType, Param.Ty->PointeeType)
                               || schemaInstMatch(*At->PointeeType, *Param.Ty->PointeeType)))
                       || (At && At->isRestricted()
                           && isIdenticalType(At->RestrictedOf, Param.Ty))
                       || (Param.Ty && Param.Ty->isRestricted()
                           && isIdenticalType(At, Param.Ty->RestrictedOf))
                       // Turbo Tier 5, Cluster A item 7: real Turbo/fpc
                       // relaxes ISO §6.6.3.3's identical-type var-parameter
                       // rule for object types specifically -- confirmed
                       // against a local fpc -Mtp build (cov3.pas): 'var A:
                       // TAnimal' accepts a 'TDog' actual, the same
                       // descendant-to-ancestor direction isAssignCompatible
                       // (SemaExpr.cpp, below) and the plain pointer-formal
                       // disjunct just above already allow.
                       || (At && Param.Ty
                           && At->Kind == TypeKind::Object
                           && Param.Ty->Kind == TypeKind::Object
                           && objectIsOrDescendsFrom(*At, *Param.Ty));
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
    if (auto* Fld = llvm::dyn_cast<FieldExpr>(&E)) {
        // EP §6.4.7: a discriminant is a value the schematic variable carries,
        // not a component of it.  It is spelled like a field and reads like
        // one, so `v.n` and `q^.n` were assignable and passable as var
        // parameters -- and codegen, which knows the body has no such field,
        // died on every one of them with "record has no field named 'n'".
        // Writing to it would claim the storage is a size it is not.
        if (const auto& RT = Fld->Record->ResolvedType;
                RT && (RT->Kind == TypeKind::Schema
                       || RT->Kind == TypeKind::SchemaInstance))
            for (const auto& D : RT->SchemaDiscs)
                if (eqCI(D.Name, Fld->Field)) return false;
        return isLValue(*Fld->Record);
    }
    if (llvm::dyn_cast<DerefExpr>(&E))               return true;
    // EP §6.5.6: a substring-variable is a variable, so a substring of a
    // variable may be assigned to.
    if (auto* Sub = llvm::dyn_cast<SubstringExpr>(&E)) return isLValue(*Sub->Str);
    // TP-only: TypeName(expr) is a variable (usable as an assignment target,
    // a var-parameter actual, or @'s operand) exactly when its OPERAND is
    // one and the two types are the same size -- see checkTypeCast's own
    // comment. Integer(SomeReal) fails this (Real and Integer are different
    // sizes) and stays a plain value, even though SomeReal is itself a
    // variable; TByteRec(SomeWord) passes it (both 2 bytes) and reinterprets
    // SomeWord's own storage. Sizes come from ResolvedType, which checkExpr
    // has already set on both nodes by the time isLValue is ever asked.
    if (auto* Tc = llvm::dyn_cast<TypeCastExpr>(&E)) {
        if (!isLValue(*Tc->Operand)) return false;
        const auto& Dst = Tc->ResolvedType;
        const auto& Src = Tc->Operand->ResolvedType;
        if (!Dst || !Src || Dst->isError() || Src->isError()) return false;
        const auto DstSz = byteSizeOf(*Dst);
        const auto SrcSz = byteSizeOf(*Src);
        return DstSz && SrcSz && *DstSz == *SrcSz;
    }
    return false;
}

bool Sema::isConformable(const Type& Formal, const Type& Actual) const {
    if (Formal.Kind != TypeKind::ConformantArray) return false;
    if (Actual.Kind != TypeKind::Array
            && Actual.Kind != TypeKind::ConformantArray)
        return false;
    // ISO §6.7.3.8 d): a packed conformant-array-form requires a packed
    // actual, and an unpacked one requires an unpacked actual -- checked at
    // every nesting level this function recurses into, same as a) below.
    if (Formal.Packed != Actual.Packed) return false;
    // ISO §6.7.3.8 a): the index-type of the actual has to be *compatible*
    // with the ordinal-type-name of this dimension's index-type-specification
    // -- e.g. a `char` schema does not conform to an `integer`-indexed actual
    // even when the element types agree.  isAssignCompatible, asked with the
    // schema's ordinal type as Dst, is this codebase's stand-in for §6.4.5
    // "compatible types" between two ordinal types (same type, or subranges
    // sharing a host type) -- the plain-Array index check above relies on the
    // same equivalence.
    if (!Formal.ConformantBounds.empty()) {
        const auto& FormalOrdTy = Formal.ConformantBounds[0].OrdType;
        auto ActualIdxTy = outerIndexTypeOf(Actual);
        if (FormalOrdTy && !FormalOrdTy->isError()
                && ActualIdxTy && !ActualIdxTy->isError()
                && !isAssignCompatible(*FormalOrdTy, *ActualIdxTy))
            return false;
    }
    if (!Formal.ElemType || !Actual.ElemType) return true;
    // ISO §6.6.3.8: the element type of the actual conforms in its turn when
    // the formal's is another schema, which is how a parameter of more than
    // one dimension is written.
    if (Formal.ElemType->Kind == TypeKind::ConformantArray)
        return isConformable(*Formal.ElemType, *Actual.ElemType);
    return isAssignCompatible(*Formal.ElemType, *Actual.ElemType);
}

// Issue #406: this walks Formal/Actual in lock-step with isConformable above
// -- same order of checks, same recursion into a formal ConformantArray's
// ElemType -- so the diagnostic it reports names whichever dimension and
// condition actually made isConformable(Formal, Actual) false, not only the
// outermost one.  The two must never disagree about what makes a pair
// (non-)conformable, or this could report a dimension that in fact conforms.
void Sema::diagnoseConformMismatch(const std::string& ParamName, SourceLocation Loc,
                                   const Type& Formal, const Type& Actual) {
    if (Actual.Kind != TypeKind::Array && Actual.Kind != TypeKind::ConformantArray) {
        error(Loc, diag::err_conformant_actual_not_array, {ParamName, Actual.Name});
        return;
    }
    if (Formal.Packed != Actual.Packed) {
        error(Loc, diag::err_conformant_packed_mismatch,
              {ParamName, Formal.Packed ? "packed" : "unpacked",
               Actual.Packed ? "packed" : "unpacked"});
        return;
    }
    if (!Formal.ConformantBounds.empty()) {
        const auto& FormalOrdTy = Formal.ConformantBounds[0].OrdType;
        auto ActualIdxTy = outerIndexTypeOf(Actual);
        if (FormalOrdTy && !FormalOrdTy->isError()
                && ActualIdxTy && !ActualIdxTy->isError()
                && !isAssignCompatible(*FormalOrdTy, *ActualIdxTy)) {
            error(Loc, diag::err_conformant_index_type_mismatch,
                  {ParamName, FormalOrdTy->Name, ActualIdxTy->Name});
            return;
        }
    }
    // ISO §6.6.3.8: a formal whose element type is itself a schema is a
    // parameter of more than one dimension -- recurse the same way
    // isConformable does, so an inner-dimension mismatch is diagnosed at the
    // dimension it actually occurs in rather than reported as this level's
    // (matching) element type.
    if (Formal.ElemType && Actual.ElemType
            && Formal.ElemType->Kind == TypeKind::ConformantArray) {
        diagnoseConformMismatch(ParamName, Loc, *Formal.ElemType, *Actual.ElemType);
        return;
    }
    error(Loc, diag::err_conformant_elem_mismatch,
          {ParamName, Formal.ElemType ? Formal.ElemType->Name : "?",
           Actual.ElemType             ? Actual.ElemType->Name : "?"});
}

// Turbo Tier 5, Cluster A item 7: object-type covariance.  Confirmed against
// a local fpc -Mtp build (cov1.pas/cov2.pas): a descendant OBJECT VALUE may
// be assigned to an ancestor-typed variable (classic Pascal "object" value
// slicing -- unlike a 'class', an object type is a value type and this is
// ordinary, not a special case), and a POINTER to a descendant object may be
// assigned to an ancestor-typed pointer variable, in both cases only in the
// descendant-to-ancestor direction: 'D: TDog; A: TAnimal; A := D;' compiles,
// 'D := A;' is "Incompatible types: got TAnimal expected TDog".  Walks the
// SAME Type::Parent chain every other Tier-5 ancestor lookup already walks.
// See also the forward declaration above (Sema::checkCallArgs's own use).
static bool objectIsOrDescendsFrom(const Type& Descendant, const Type& Ancestor) {
    for (const Type* Cur = &Descendant; Cur; Cur = Cur->Parent.get())
        if (Cur == &Ancestor) return true;
    return false;
}

bool Sema::isAssignCompatible(const Type& Dst, const Type& Src,
                              bool ExactBounds, int Depth) const {
    if (Dst.isError() || Src.isError())   return true;  // suppress cascades

    // EP §6.4.6 a): two types that are the same are assignment-compatible only
    // if the type may be the component-type of a file, which §6.4.3.6 says a
    // restricted type may not be.  So no value is assignment-compatible with a
    // restricted type, and a value of one is compatible with nothing: the
    // assignments it takes part in are the ones §6.9.2.2 and §6.7.3 name.
    if (Dst.isRestricted() || Src.isRestricted()) return false;

    // The same type is the same type.  Also what stops a record reachable from
    // itself through a pointer from recursing without end below.
    //
    // Below the restricted check on purpose: EP §6.4.6 a) makes a restricted
    // type assignment-compatible with NOTHING, its own self included, and an
    // identity shortcut above that answered `w := w` with yes and took the
    // rule's own diagnostic with it.
    if (&Dst == &Src) return true;

    // EP §6.4.3.3 makes `string` a schema whose one discriminant is the
    // capacity, so every string(n) IS an instance of it.  That is what a
    // `var s: string` formal accepts, and what isVarStringLike above cannot
    // settle on its own: this is the SCHEMA against a plain VarString, not two
    // strings.
    if (isVarStringLike(&Dst) && isVarStringLike(&Src)
            && (Dst.Kind == TypeKind::Schema || Src.Kind == TypeKind::Schema))
        return true;

    // EP §6.4.7: SchemaInstance — compatible if same schema+discriminant values,
    // or fall through to body-type compatibility.
    if (Dst.Kind == TypeKind::SchemaInstance && Src.Kind == TypeKind::SchemaInstance) {
        // A NAME is not an identity -- the sentence this release is about, and
        // schemas were the one kind still comparing spellings after records and
        // enums were given declaration identity.  Two `vec(3)` from different
        // declarations were "the same type", so a 30-element one was assigned
        // into a 3-element one: 240 bytes into 24, and a segfault.
        //
        // Where both sides know their declaration it settles the question.
        // Where one does not -- separate compilation gives the same schema a
        // different node in each unit -- fall through to comparing the BODIES,
        // which accepts an identical shape and rejects a different one.
        const bool SameDecl = !Dst.SchemaBodyNode || !Src.SchemaBodyNode
                           || Dst.SchemaBodyNode == Src.SchemaBodyNode;
        // Pascal identifiers are case-insensitive (eqCI, as the Schema arm
        // just below already uses); a bare `==` here was academic while
        // SameDecl gated it, but it stopped mattering only by accident of the
        // body-comparison fallback below silently correcting a mismatch,
        // rather than the comparison being right.
        if (eqCI(Dst.SchemaName, Src.SchemaName) && SameDecl
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
        // Same rule for an undiscriminated formal: it accepts instances of the
        // schema it names, not of anything spelled alike.
        if (Other.Kind == TypeKind::Schema || Other.Kind == TypeKind::SchemaInstance)
            return eqCI(Sch.SchemaName, Other.SchemaName)
                && (!Sch.SchemaBodyNode || !Other.SchemaBodyNode
                    || Sch.SchemaBodyNode == Other.SchemaBodyNode);
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
            //
            // ShortString (Turbo string[N]) now sits alongside VarString,
            // unlike when this comment was first written: VarString's
            // unconditional true means any two string(N)s are assignment-
            // compatible regardless of capacity, safe because CodeGen's
            // plang_str_assign truncates across differently-sized buffers at
            // run time.  ShortString now has the identical run-time support
            // -- plang_sstr_assign (runtime/plang_sstr.cpp), truncating
            // rather than erroring, wired in through StringCallMarshalling::
            // emitSstrStore -- so `s: string[5] := t: string[10]` is exactly
            // as safe here as the VarString case immediately above it, and
            // for the same reason.
            case TypeKind::Integer: case TypeKind::Real: case TypeKind::Boolean:
            case TypeKind::Char:    case TypeKind::String: case TypeKind::Nil:
            case TypeKind::VarString:
            case TypeKind::ShortString:
                return true;

            // ISO §6.4.2.3: each enumerated-type definition is a distinct
            // type, so two of them agree only when they are the same
            // declaration — by name, or by identity when written inline.
            // ISO §6.4.2.3: each enumerated-type definition is a distinct
            // type, so two of them agree only when they are the same
            // declaration.  Comparing NAMES is not that -- two enumerations in
            // different scopes may share a spelling and share nothing else, and
            // `(mon,tue,wed,thu)` was assignable to a variable of
            // `(red,green,blue)`, putting the ordinal 3 in a type whose largest
            // is 2.  The value list is what distinguishes them, and it agrees
            // for the same declaration reached through separate compilation,
            // where the declaring node does not.
            case TypeKind::Enum:
                if (isAnonymousNominal(Dst) || isAnonymousNominal(Src))
                    return &Dst == &Src;
                return Dst.Name == Src.Name && Dst.EnumValues == Src.EnumValues;

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
            case TypeKind::Record: {
                // A NAME is not an identity.  This compared spellings, so a
                // `record a: integer end` at program scope accepted a
                // `record a, b, c: integer end` declared in a procedure and
                // sharing the name: 24 bytes copied into 8, quietly.
                //
                // The declaration is the identity (ISO §6.4.3.3), and where
                // both sides have one it settles the question outright.  Where
                // they do not -- separate compilation gives the same
                // declaration a different node in each unit -- the shape has to
                // answer instead, so the name is necessary but no longer
                // sufficient.
                if (!isAnonymousNominal(Dst) || !isAnonymousNominal(Src)) {
                    if (Dst.Name != Src.Name) return false;
                    if (Dst.RecordDecl && Src.RecordDecl
                            && Dst.RecordDecl == Src.RecordDecl)
                        return true;
                }
                // The cap exists so a record reachable from itself only
                // through a chain of distinct anonymous record types (no
                // RecordDecl to short-circuit on) cannot recurse without end;
                // it is not license to call two types compatible once the
                // structure below it goes unexamined.  Two records that were
                // never shown equal are not equal, so this refuses rather
                // than misjudges -- the same conservative default the
                // restricted-type check above makes when it cannot say yes.
                // A false negative here is a diagnostic to rename or
                // restructure a type; a false positive is a value of one
                // layout copied over another's.
                if (Depth > 16) return false;
                // ISO §6.4.3.1: `packed` is part of what the type IS, and two
                // records that differ in it have different layouts.  Ignoring
                // it let a padded record be stored into a packed one --
                // `b := a` printed 504403158265495552 for a field holding 7.
                if (Dst.Packed != Src.Packed) return false;
                if (Dst.RecordFields.size() != Src.RecordFields.size()) return false;
                for (size_t I = 0; I < Dst.RecordFields.size(); ++I) {
                    if (!Dst.RecordFields[I].Ty || !Src.RecordFields[I].Ty) return false;
                    if (!isAssignCompatible(*Dst.RecordFields[I].Ty,
                                            *Src.RecordFields[I].Ty,
                                            /*ExactBounds=*/true, Depth + 1))
                        return false;
                }
                return true;
            }

            // ISO §6.4.5 c): two set-types are compatible when their base-types
            // are, so the bounds of the base are not asked to agree.
            case TypeKind::Set:
                if (Src.Name == "set literal" || Src.Name == "[]") return true;
                if (!Dst.ElemType || !Src.ElemType) return Dst.Name == Src.Name;
                return isAssignCompatible(*Dst.ElemType, *Src.ElemType);

            // Pointer: ISO §6.4.4 requires the two pointer-types' domain
            // types to be the SAME type, not merely assignment-compatible
            // with one another -- there is no covariance or contravariance
            // through a pointer.  Recursing into isAssignCompatible on the
            // pointees let a subrange's own compatibility with its base type
            // leak through a pointer: `^integer` and `^(1..10)` were
            // accepted either way round, though a range check the subrange
            // requires never runs on a value read back through the integer
            // pointer.
            //
            // Pointer types are interned by pointee identity (see
            // TypeContext::getPointer) and Enum/Record types carry their own
            // declaration as their identity (ISO §6.4.2.3, §6.4.3.3), so two
            // pointers to the same domain type are already the same Type
            // object and were caught by the `&Dst == &Src` shortcut above.
            // Reaching here with Dst.Kind == Src.Kind == Pointer means the
            // domain types are genuinely different, so isIdenticalType --
            // the same canonical-identity test ISO §6.6.3.3 uses for a var
            // parameter -- is what settles it, not the general assignability
            // relation.
            //
            // EP §6.4.7 is the one deliberate exception: a SchemaInstance
            // pointee is NEVER interned (TypeContext::getPointer above
            // notwithstanding -- see its own comment), so two independently
            // written `^Vec(4)` aliases mint two distinct pointee Type
            // objects for the identical instantiation, and isIdenticalType
            // alone wrongly calls them different domains (issue #407, a
            // regression from this very fix). schemaInstMatch is the same
            // declaration+discriminant-value identity check the var-parameter
            // call site above (checkCallArgs) already falls back to for
            // exactly this reason; it returns false outright for any pointee
            // kind other than SchemaInstance, so it adds this one carve-out
            // without loosening isIdenticalType's identity requirement for
            // Subrange or any other pointee kind.
            //
            // Turbo's generic `Pointer` (TypeContext::getGenericPointer) is
            // the one deliberate exception to "both domains present": it has
            // no PointeeType by construction -- the same "no specific
            // pointee" state Nil's own carve-out a few lines below models --
            // and is compatible with any pointer type, typed or generic,
            // in either direction: `p := q` and `q := p` both compile for
            // `p: Pointer; q: ^Integer`, matching real Turbo Pascal, where
            // Pointer is untyped and no domain check applies to it at all.
            case TypeKind::Pointer:
                if (!Dst.PointeeType || !Src.PointeeType) return true;
                // Turbo Tier 5, Cluster A item 7: a POINTER to a descendant
                // object type is assignment-compatible with an ancestor-
                // typed pointer -- see objectIsOrDescendsFrom's own comment
                // above (cov1.pas: 'PA: ^TAnimal; PA := @D;' for 'D: TDog').
                // Checked ahead of isIdenticalType, which would otherwise
                // say no (the two pointee Type objects are genuinely
                // different declarations, not merely differently-spelled
                // aliases of one).
                if (Dst.PointeeType->Kind == TypeKind::Object
                        && Src.PointeeType->Kind == TypeKind::Object
                        && objectIsOrDescendsFrom(*Src.PointeeType, *Dst.PointeeType))
                    return true;
                return isIdenticalType(Dst.PointeeType, Src.PointeeType)
                    || schemaInstMatch(*Dst.PointeeType, *Src.PointeeType);

            // Turbo procedural VALUES: ISO §6.6.3.6's own congruity rule --
            // same parameter shapes and, for a function, the same result
            // type -- is exactly what a procedural VARIABLE's assignment
            // needs too, and congruousSignature already implements it
            // (recursing through sameParamType for a nested procedural
            // parameter).  Not reached for a procedural PARAMETER's own
            // congruity check, which calls congruousSignature directly
            // (checkProcedureActual) rather than through an assignment;
            // this arm exists for isAssignCompatible's OWN callers --
            // checkAssign chief among them, once Dst.Kind/Src.Kind are both
            // Procedure or both Function -- which had no case here at all
            // before procedural VARIABLES existed to reach it, and fell to
            // the default `Dst.Name == Src.Name` string comparison below,
            // a strictly weaker and coincidence-prone stand-in for the real
            // structural check.
            case TypeKind::Procedure:
            case TypeKind::Function:
                return congruousSignature(Dst, Src);

            // Turbo Tier 5, Cluster A item 7: object VALUE covariance -- see
            // objectIsOrDescendsFrom's own comment above.  Src may be Dst's
            // own type (the '&Dst == &Src' shortcut above already covers
            // that, but the ancestor chain walk finds it too) or any
            // descendant of it; the reverse is refused, matching fpc.
            case TypeKind::Object:
                return objectIsOrDescendsFrom(Src, Dst);

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
    // Turbo procedural VALUES: 'f := nil' clears a procedural variable, and
    // is what Assigned(f) then reports false for -- the same relationship
    // Pointer/Nil just above already has.
    if ((Dst.Kind == TypeKind::Procedure || Dst.Kind == TypeKind::Function)
            && Src.Kind == TypeKind::Nil) return true;
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
    if (isVarStringLike(&Dst) && isVarStringLike(&Src)) return true;
    // VarString(N) ← char
    if (isVarStringLike(&Dst) && Src.Kind == TypeKind::Char)   return true;
    // VarString(N) ← plain string literal / String
    if (isVarStringLike(&Dst) && Src.Kind == TypeKind::String)  return true;
    // String (7185) ← VarString: allow for passing to legacy write/writeln
    if (Dst.Kind == TypeKind::String && isVarStringLike(&Src))  return true;

    // Turbo string[N] compatibility -- the ShortString-specific siblings of
    // the two VarString←Char/String rules just above.  Not folded into
    // those (e.g. by widening isVarStringLike's own OR chain): ShortString
    // and VarString are separate, incompatible runtime layouts, and
    // deciding "may X be assigned to a ShortString" is a genuinely
    // different question from the VarString one even where the answer
    // happens to be the same shape.  ShortString(N) ← char.
    if (isShortStringLike(&Dst) && Src.Kind == TypeKind::Char)   return true;
    // ShortString(N) ← plain string literal / String.
    if (isShortStringLike(&Dst) && Src.Kind == TypeKind::String) return true;

    // ISO §6.4.3.2: a string-type takes a string value of exactly its own
    // length.  A literal arrives as String or, in EP, as VarString carrying
    // its length; checkStringCapacity is what holds it to the exact length,
    // since a String does not record one.
    // Two string-types are two arrays, and were already settled above.
    if (isCharStringType(Dst)) {
        if (Src.Kind == TypeKind::String) return true;
        if (Src.Kind == TypeKind::VarString)
            // A discriminant-fixed capacity is not known here, so whether the
            // lengths match is a run-time question rather than a reason to
            // refuse the program.
            return Src.ExtentVaries
                || Src.StrCapacity == charStringLength(Dst);
    }
    // A string-type is a string value, so it goes where one is expected.
    if (isVarStringLike(&Dst) && isCharStringType(Src))
        return true;
    // EP §6.4.5(d): "T1 is either a string-type or the char-type and T2 is
    // either a string-type or the char-type" -- compatible in EITHER
    // direction, not only char-to-string. The char-to-string half was here
    // (isVarStringLike(&Dst) && Src.Kind==Char, and Dst.Kind==String &&
    // Src.Kind==Char, above); the reverse -- a string-type value assigned to
    // a char VARIABLE -- was missing entirely, so `c := s` for `c: char` was
    // refused outright rather than deferred to §6.4.6(f)'s run-time length
    // check (c)'s "length of the value of T2 is <= the capacity of T1",
    // capacity 1 for char (§6.4.3.3.1).
    if (Dst.Kind == TypeKind::Char
            && (isVarStringLike(&Src) || isCharStringType(Src)
                || Src.Kind == TypeKind::String))
        return true;

    // Turbo: a zero-based array of Char decays to the ADDRESS of its first
    // element when assigned to a PChar-like pointer -- `p := buf` for
    // `buf: array[0..9] of Char`, no `@` needed.  Confirmed against real
    // `fpc -Mtp`, gated the same "pointee is Char" way as the arithmetic and
    // indexing rules above (isCharPointerType, Type.h) plus Opts.turbo().
    //
    // Deliberately does NOT touch isCharStringType just above: that is ISO
    // §6.4.3.2's canonical string-type, `packed array[1..n] of char` with
    // SubLo == 1, and it keeps meaning "this is a Pascal string value" here
    // exactly as it always has -- a 1-based char array is still refused as a
    // PChar source (real fpc refuses the identical program: "Incompatible
    // types: got array[1..9] Of Char expected PChar").  The rule below is
    // therefore checked independently and structurally rather than by
    // widening isCharStringType's own SubLo == 1 condition, which must stay
    // untouched for every OTHER caller (string concatenation, comparison,
    // length/substr/trim -- ISO 10206 §6.4.3.3.1's "canonical-string-type"
    // machinery) that still means exactly what it always has.  `packed` is
    // not required either, matching fpc: an unpacked zero-based char array
    // decays too.
    if (Opts.turbo() && isCharPointerType(Dst)
            && Src.Kind == TypeKind::Array && Src.ElemType
            && Src.ElemType->Kind == TypeKind::Char
            && Src.IndexType && Src.IndexType->Kind == TypeKind::Subrange
            && Src.IndexType->SubLo == 0)
        return true;
    return false;
}

std::shared_ptr<Type> Sema::numericResult(const Type& L, const Type& R) {
    if (L.Kind == TypeKind::Complex || R.Kind == TypeKind::Complex) return TyComplex;
    if (L.Kind == TypeKind::Real    || R.Kind == TypeKind::Real)    return TyReal;
    return commonIntType(L, R);
}

// See the declaration (Sema.h) for the contract.  Both L and R are already
// known integral by every call site (isIntegral()/isNumeric() gates run
// first), so each is either TypeKind::Integer or a Subrange whose SubBase
// chain bottoms out at one -- both carry a real, meaningful Width/IsSigned
// of their own (TypeContext::getSubrange stamps a subrange's from its host,
// narrowed under Turbo; Type::Width's own comment). Reading L.Width/
// R.Width/L.IsSigned/R.IsSigned directly, with no SubBase-peeling first, is
// therefore already correct for both.
//
// ISO 7185 and Extended Pascal mint exactly one Integer type -- Width==64,
// IsSigned==true, the DefaultIntWidth==64 case -- so for those two dialects
// L and R are always that identical (Width,IsSigned) pair, LW==RW is always
// true, and Ctx_.getInt(64, true) returns the exact same cached TyInt_
// object (TypeContext::IntCache_ is keyed on {Width,Signed}, and TyInt_
// itself was minted from that identical call) that plain `return TyInt`
// used to hand back directly: a true no-op, not merely an equal-looking
// type, for every ISO/EP program.
//
// Only Turbo's sized-integer ladder, where two Integer-kind operands can
// genuinely differ in Width and/or IsSigned, ever takes the non-trivial
// branch below.  The tie-break rules are checked against real `fpc -Mtp`
// field practice, not assumed: the WIDER operand's own sign wins when
// widths differ (`LongInt + Word` stays signed even though Word is
// unsigned -- the signed 32-bit result can represent every Word value, so
// nothing is lost going that way), and when widths are equal but signs
// differ the result is unsigned (`Word + ShortInt` comes back unsigned),
// matching C's/Delphi's usual-arithmetic-conversion rule.
std::shared_ptr<Type> Sema::commonIntType(const Type& L, const Type& R) {
    if (L.Width == R.Width)
        return Ctx_.getInt(L.Width, L.IsSigned && R.IsSigned);
    return L.Width > R.Width ? Ctx_.getInt(L.Width, L.IsSigned)
                              : Ctx_.getInt(R.Width, R.IsSigned);
}
