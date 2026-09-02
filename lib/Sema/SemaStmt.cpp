#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SemaUtil.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <format>
#include <ranges>

using namespace plang;

// See NumStmtKinds in AstBase.h.  checkStmt returns quietly for a statement it
// does not know, which for a checker means the statement was approved.
static_assert(NumStmtKinds == 15, "a new statement needs a case in checkStmt");

// ---------------------------------------------------------------------------
// Statement checking
// ---------------------------------------------------------------------------

void Sema::checkStmt(const StmtNode* Stmt) {
    if (!Stmt) return;

    if (auto* S = llvm::dyn_cast<AssignStmt>(Stmt))    { checkAssign(*S);   return; }
    if (auto* S = llvm::dyn_cast<CompoundStmt>(Stmt))  { checkCompound(*S); return; }
    if (auto* S = llvm::dyn_cast<GotoStmt>(Stmt))      { checkGoto(*S);     return; }
    if (auto* S = llvm::dyn_cast<LabeledStmt>(Stmt))   { checkLabeled(*S);  return; }
    if (auto* S = llvm::dyn_cast<CallStmt>(Stmt))      { checkCallStmt(*S); return; }
    if (auto* S = llvm::dyn_cast<MethodCallStmt>(Stmt)) { checkMethodCallStmt(*S); return; }
    if (auto* S = llvm::dyn_cast<InheritedCallStmt>(Stmt)) { checkInheritedCallStmt(*S); return; }
    if (auto* S = llvm::dyn_cast<IndirectCallStmt>(Stmt)) { checkIndirectCallStmt(*S); return; }
    // Structured statements: push onto StructStack so checkGoto can determine
    // whether a goto would jump INTO this statement from outside it (ISO §6.8.1).
    if (auto* S = llvm::dyn_cast<IfStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkIf(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<WhileStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkWhile(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<ForStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkFor(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<ForInStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkForIn(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<RepeatStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkRepeat(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<WithStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkWith(*S); StructStack.pop_back(); return;
    }
    if (auto* S = llvm::dyn_cast<CaseStmt>(Stmt)) {
        StructStack.push_back(Stmt); checkCase(*S); StructStack.pop_back(); return;
    }
}

// ---------------------------------------------------------------------------
// Reachability
//
// Only two things in Pascal leave a statement without coming back: a goto, and
// the halt that Extended Pascal adds.  Turbo adds four more of the same shape
// -- Exit, Break, Continue, RunError -- each a required procedure the same
// way Halt is, so a call to one is recognized the same way (see
// callsTransferBuiltin).  Everything else completes, so a statement following
// one of those is reached by nothing — unless it carries a label, which puts
// it back within reach of any goto that names it.
//
// The analysis errs one way on purpose.  Treating a statement as completing
// when it does not costs a warning that was not given; the opposite would call
// live code dead, and a warning that is wrong is worse than one that is
// missing.
// ---------------------------------------------------------------------------

namespace {

bool sequenceTransfers(const std::vector<std::unique_ptr<StmtNode>>& Stmts,
                        const SymbolTable& Symtab);

// Whether S calls one of the required procedures that never returns to what
// follows it -- resolved to the symbol it names, not matched on how that
// name is spelled.  A program is free to declare its own procedure called
// `halt` or `exit`, which shadows the builtin (ISO §6.2.2.10) and returns
// like any other call; only the builtin itself never does.  Under a dialect
// that does not require a given one of these (Halt is EP|TP; Exit/Break/
// Continue/RunError are TP-only), Symtab still resolves the name to the same
// Symbol -- registerBuiltins declares every name in every dialect and only
// flags NotInDialect -- so a call the parser accepted at all is already one
// Sema either approved or already reported an error on; this check does not
// need to ask the dialect question again.
bool callsTransferBuiltin(const CallStmt& C, const SymbolTable& Symtab) {
    const Symbol* Callee = Symtab.lookup(C.Name);
    return Callee && Callee->Kind == SymbolKind::Builtin
                   && builtinAlwaysTransfers(Callee->BuiltinKind);
}

bool alwaysTransfers(const StmtNode* S, const SymbolTable& Symtab) {
    if (!S) return false;
    if (llvm::isa<GotoStmt>(S)) return true;
    if (auto* C = llvm::dyn_cast<CallStmt>(S)) return callsTransferBuiltin(*C, Symtab);
    if (auto* C = llvm::dyn_cast<CompoundStmt>(S)) return sequenceTransfers(C->Stmts, Symtab);
    if (auto* W = llvm::dyn_cast<WithStmt>(S))     return alwaysTransfers(W->Body.get(), Symtab);
    // An if reaches past itself unless neither arm does, which needs both arms
    // to exist.  A case is left alone: whether it can be fallen out of depends
    // on whether the arms cover the selector, and that is a separate question.
    if (auto* I = llvm::dyn_cast<IfStmt>(S))
        return I->Else && alwaysTransfers(I->Then.get(), Symtab)
                       && alwaysTransfers(I->Else.get(), Symtab);
    // A loop may run no times, a labeled statement may be jumped into, and
    // everything else simply finishes.
    return false;
}

bool sequenceTransfers(const std::vector<std::unique_ptr<StmtNode>>& Stmts,
                        const SymbolTable& Symtab) {
    bool Gone = false;
    for (const auto& St : Stmts) {
        if (!St) continue;                                  // the empty statement
        if (llvm::isa<LabeledStmt>(St.get())) Gone = false; // a goto can land here
        if (!Gone && alwaysTransfers(St.get(), Symtab)) Gone = true;
    }
    return Gone;
}

} // namespace

void Sema::warnUnreachable(const std::vector<std::unique_ptr<StmtNode>>& Stmts) {
    bool Gone = false, Reported = false;
    for (const auto& St : Stmts) {
        if (!St) continue;
        if (llvm::isa<LabeledStmt>(St.get())) {
            Gone = Reported = false;
        } else if (Gone && !Reported) {
            // One report for each run of unreachable statements: the rest of
            // the run is the same mistake, and naming it once is enough.
            warning(St->Loc, diag::warn_unreachable_code, {});
            Reported = true;
        }
        if (!Gone && alwaysTransfers(St.get(), Symtab)) Gone = true;
    }
}

void Sema::checkCompound(const CompoundStmt& S) {
    warnUnreachable(S.Stmts);
    for (const auto& St : S.Stmts) checkStmt(St.get());
}

Sema::FuncFrame* Sema::resultFrameFor(const std::string& Name) {
    // A declaration closer in than the function's own denotes something else by
    // the name, and the innermost declaration is the one that wins.  The
    // function's own identifier is deliberately not in the symbol table — that
    // is what leaves a recursive call able to find the function — so anything
    // found there is a nearer declaration.
    // Issue #626: an object field (own or ancestor-inherited) exposed
    // unqualified by pushMethodSelfScope shares this scope-lookup with
    // every genuine local, but must NOT count as a shadowing declaration
    // here -- fpc -Mtp confirms the method's own implicit result binding
    // wins a bare `MethodName := value` even when an ancestor field of the
    // same name is in scope (the assignment sets the result, not the
    // field); only a REAL local (or nested function's own local, the
    // ShadowedByPlainVar scenario this already handled) shadows it.
    const Symbol* Sym = Symtab.lookup(Name);
    const bool ShadowedByPlainVar = Sym
        && (Sym->Kind == SymbolKind::Var || Sym->Kind == SymbolKind::VarParam)
        && !Sym->IsResultVar && !Sym->IsSelfScopeField;
    const bool Shadowed = Sym && (Sym->Kind == SymbolKind::Var
                                  || Sym->Kind == SymbolKind::VarParam)
                               && !Sym->IsSelfScopeField;

    // Innermost first: a function nested inside one of the same name has its
    // own result meant by it.
    for (auto It = FuncStack.rbegin(); It != FuncStack.rend(); ++It) {
        const ProcDecl* D = It->Decl;
        if (!D) continue;
        // EP §6.7.2: a named result variable is in the symbol table by design,
        // being the one form of the result that is written as a variable --
        // but that cuts both ways.  It was never checked against Shadowed at
        // all, so a nested function's own unrelated local of the same
        // spelling as an ENCLOSING function's ResultName was hijacked: `type
        // of x` aside, `function outer = result: integer; function inner:
        // integer; var result: real; begin result := 5.5; ...` typed
        // `result` inside inner as outer's INTEGER, not inner's own REAL,
        // because this branch walked past inner's local straight to outer's
        // frame.  ShadowedByPlainVar is false exactly when Sym IS some
        // frame's own result variable (or there is no shadow at all), which
        // is what lets a function's own self-reference through unaffected.
        if (!ShadowedByPlainVar
                && !D->ResultName.empty() && eqCI(D->ResultName, Name))
            return &*It;
        if (!Shadowed && eqCI(D->Name, Name)) return &*It;
    }
    return nullptr;
}

const Sema::FuncFrame* Sema::resultFrameFor(const std::string& Name) const {
    return const_cast<Sema*>(this)->resultFrameFor(Name);
}

// ISO §6.8.2.2: an assignment's target is either a variable-access or the
// identifier of a function whose block contains it, which stands for that
// activation's result.  The clause says contain rather than be, so a function
// declared inside another may name the outer one's result.
bool Sema::isFunctionResultTarget(const ExprNode& Target) const {
    const auto* Id = llvm::dyn_cast<IdentExpr>(&Target);
    return Id && resultFrameFor(Id->Name) != nullptr;
}

void Sema::checkAssign(const AssignStmt& S) {
    // -std=turbo (checkIdent's own comment has the full rule): find the
    // ROOT of this target -- 'result' alone in both 'result := x' and
    // 'result.f := x' -- BEFORE resolving it, so checkIdent can recognize
    // that exact node by pointer identity while checkExpr(*S.Target)
    // recurses into it below.  Same index/field walk as the HasResult
    // marking further down, computed a second time here rather than
    // reusing that one: this one has to run BEFORE checkExpr(*S.Target),
    // and that one depends on nothing checkExpr produces either, so the
    // duplication is only two identical small loops, not two different
    // answers.
    const ExprNode* TargetRootForIdent = S.Target.get();
    while (TargetRootForIdent) {
        if (auto* Ix = llvm::dyn_cast<IndexExpr>(TargetRootForIdent))
            { TargetRootForIdent = Ix->Array.get();  continue; }
        if (auto* Fe = llvm::dyn_cast<FieldExpr>(TargetRootForIdent))
            { TargetRootForIdent = Fe->Record.get(); continue; }
        break;
    }
    const IdentExpr* SavedAssignTargetRoot = CurAssignTargetRoot_;
    CurAssignTargetRoot_ = llvm::dyn_cast_or_null<IdentExpr>(TargetRootForIdent);

    // The target is resolved before it is judged, because whether it is
    // assignable can depend on what it turned out to be: `v.n` is a field in
    // the syntax and a schema discriminant in the type, and only the second
    // says it cannot be written to.
    auto Dst = checkExpr(*S.Target);
    CurAssignTargetRoot_ = SavedAssignTargetRoot;
    if (!isLValue(*S.Target)) {
        // ISO §6.6.3.1: a functional parameter reads like the function-result
        // variable of the enclosing function, so say which one it is rather
        // than leaving the reader to work out why the name is not assignable.
        if (auto* Id = llvm::dyn_cast<IdentExpr>(S.Target.get())) {
            const Symbol* Sym = Symtab.lookup(Id->Name);
            if (Sym && Sym->IsProcParam) {
                error(S.Loc, diag::err_proc_param_not_assignable, {Id->Name});
                (void)checkExpr(*S.Value);
                return;
            }
        }
        error(S.Loc, diag::err_lhs_not_lvalue);
    }

    // Assignment to a function-result pseudo-variable satisfies §6.7.3 for the
    // function that owns the result, which need not be the one being checked.
    // Asked of the ROOT of the access path, not only a bare identifier target
    // -- §6.7.3 is satisfied by assigning any part of the result, and a
    // function returning a record is ordinarily written field by field
    // (`result.x := 1; result.y := 2`), which this missed entirely: every
    // field was legitimately assigned and the function was still refused as
    // not assigning to its result at all.  A dereference stops the walk on
    // purpose, matching checkNotProtected's own walk for the same reason:
    // `result^.f := x` for a result that is a pointer writes through what it
    // points at, not to the result variable itself.
    {
        const ExprNode* Root = S.Target.get();
        while (Root) {
            if (auto* Ix = llvm::dyn_cast<IndexExpr>(Root)) { Root = Ix->Array.get();  continue; }
            if (auto* Fe = llvm::dyn_cast<FieldExpr>(Root)) { Root = Fe->Record.get(); continue; }
            break;
        }
        if (auto* Id = llvm::dyn_cast_or_null<IdentExpr>(Root))
            if (FuncFrame* F = resultFrameFor(Id->Name)) F->HasResult = true;
    }

    // EP §6.7.3.1: detect assignment to a protected parameter.
    // Also covers A[i] := ... where A is a protected conformant array param.
    checkNotProtected(*S.Target, S.Loc);

    // A bare set literal (`s := [999]`, no E.TypeName of its own) has no way
    // to know Dst is coming by the time generic checkExpr reaches
    // checkSetLit -- routing it there directly, with Dst as a hint, lets an
    // out-of-range element be caught the same warn-then-trap way a named
    // constructor's already is, instead of checkSetLit's own context-free
    // element-count check (sized for a literal with no destination at all)
    // firing first and refusing to compile.
    std::shared_ptr<Type> Src;
    if (Dst->Kind == TypeKind::Set) {
        if (auto* SL = llvm::dyn_cast<SetLiteralExpr>(S.Value.get())) {
            Src = checkSetLit(*SL, Dst);
            // checkExpr's generic dispatch is what normally stamps this --
            // bypassing it here means doing that stamp by hand, both for
            // codegen (which reads ResolvedType off the AST node) and so
            // isLooseSet sees a Set-kind type if anything downstream still
            // asks (adoptSetType below is a no-op either way once Src is
            // already exactly Dst).
            S.Value->ResolvedType = Src;
        }
    }
    // Turbo procedural VALUES: 'f := g' where f's declared type is itself
    // callable (Procedure/Function -- ISO 7185/Extended Pascal have no
    // syntax that can make this true, only -std=turbo's own procedural type
    // denoter, see ParseType.cpp) and g bare-names a routine.  Disambiguates
    // the two readings a bare Proc-kind identifier can have on the right of
    // ':=' by the ASSIGNMENT TARGET's type, per this feature's own design:
    // an ordinary ("f := g" where f is an Integer function, say) target
    // leaves this branch untaken and falls through to checkExpr below
    // exactly as before, still reading g as ISO §6.7.3's implicit
    // zero-argument call -- the ubiquitous "assign a function's own result
    // via its own name" idiom this must not disturb.  '@g' needs no help
    // here: checkUnary's own arm resolves it the same way from the operator
    // alone, so checkExpr(*S.Value) below already gets the right answer for
    // that spelling once it recurses into checkUnary.
    //
    // Issue #649: the identical disambiguation applies when g is ALREADY a
    // procedural VARIABLE rather than a declared routine ("f2 := f1" copying
    // one procedural value into another) -- checkIdent's own new default for
    // a bare, function-typed procedural variable is to auto-call it
    // (ISO Sec6.7.3's function-designator, matching a plain function's own
    // bare name), so without this, "f2 := f1" would call f1 and try to
    // assign its RESULT to f2 instead of copying f1's value.  Marked here,
    // ahead of checkExpr, rather than through a checkRoutineValue-shaped
    // dedicated resolver: isProcVarNameCandidate's own comment (Sema.h)
    // explains why the ordinary checkExpr/checkIdent path already answers
    // correctly once Id.Resolution says which reading this occurrence is.
    if (!Src && isCallable(*Dst)) {
        if (auto* Id = llvm::dyn_cast<IdentExpr>(S.Value.get())) {
            if (isRoutineNameCandidate(*Id))
                Src = checkRoutineValue(*Id);
            else if (isProcVarNameCandidate(*Id))
                Id->Resolution = IdentExpr::IdentResolution::ProcVarRawValue;
        }
    }
    if (!Src) Src = checkExpr(*S.Value);

    // EP §6.9.2.2: the value has to suit the type of the variable — except for
    // a function result, where it has to suit that type's underlying type.
    // That exception is the whole of how a function returning a restricted
    // type is written: it works in the underlying type and assigns that.
    if (Dst->isRestricted() && isFunctionResultTarget(*S.Target))
        Dst = Dst->RestrictedOf;

    // §6.8.2.2: neither side of an assignment may possess a file type or a
    // type with a file component.  A file names something outside the program
    // that the variable is a window onto, and there is no meaning to be had
    // from copying the window: both names would be left on one file, and which
    // of them the next read moved would depend on nothing in the program.
    if (!Dst->isError() && typeContainsFile(*Dst))
        error(S.Loc, diag::err_assign_file);
    else if (!Src->isError() && typeContainsFile(*Src))
        error(S.Loc, diag::err_assign_file);
    else if (!Dst->isError() && !Src->isError() && !isAssignCompatible(*Dst, *Src)) {
        if (Dst->isRestricted())
            error(S.Loc, diag::err_restricted_assigned, {Dst->Name});
        else if (Src->isRestricted())
            error(S.Loc, diag::err_restricted_used, {Src->Name});
        else if (Src->Kind == TypeKind::Record && Dst->Kind == TypeKind::Record
                 && Src->Packed != Dst->Packed)
            error(S.Loc, diag::err_assign_mismatch_packed, {Src->Name, Dst->Name});
        else if (!Src->Name.empty() && Src->Name == Dst->Name)
            error(S.Loc, diag::err_assign_mismatch_homonym, {Src->Name});
        else
            error(S.Loc, diag::err_assign_mismatch, {Src->Name, Dst->Name});
    }
    checkStringCapacity(*Dst, *S.Value);
    warnIfConstantOutOfRange(*Dst, *S.Value);
    // warnIfConstantOutOfRange only looks at Dst.Kind == Subrange; a set's own
    // constant-out-of-range members are checked here instead, against the
    // base type Dst names -- an untyped `[999]` only learns it is a TinySet
    // (set of 1..10) from Dst, so this has to wait for Dst the same way
    // adoptSetType below does.
    if (Dst->Kind == TypeKind::Set && Dst->ElemType)
        warnIfSetLitOutOfRange(*Dst->ElemType, *S.Value);
    adoptSetType(*S.Value, Dst);
}

// §6.4.6, §6.8.2.2: the value assigned to a variable of a subrange type shall
// be within that subrange.  The check is made when the program runs, because
// in general the value is not known before then — but when it is a constant it
// is known now, and the run-time trap is certain wherever the statement is
// reached.  Still a warning and not an error: an assignment nothing reaches
// commits no error, and rejecting the program would refuse one the standard
// admits.
void Sema::warnIfConstantOutOfRange(const Type& Dst, const ExprNode& Src) {
    if (Dst.Kind != TypeKind::Subrange) return;
    // EP §6.4.7: bounds a discriminant fixes are not known here.  The recorded
    // ones are the probe's, so this warned that every value but 1 was outside
    // 1..1 -- certain of a trap that does not happen, on a program that is
    // correct.  Codegen checks it against the value the object carries.
    if (Dst.ExtentVaries) return;
    auto V = constBound(Src);
    if (!V) return;
    if (*V >= Dst.SubLo && *V <= Dst.SubHi) return;
    warning(Src.Loc, diag::warn_const_out_of_range,
            {spellOrdinal(Dst, *V), spellOrdinal(Dst, Dst.SubLo),
             spellOrdinal(Dst, Dst.SubHi)});
}

void Sema::checkStringCapacity(const Type& Dst, const ExprNode& Src) {
    // Only a literal has a length the compiler knows; a string variable's
    // capacity is an upper bound on its length, not the length itself, so
    // assigning a wider one is not by itself an error.
    const auto* Lit = llvm::dyn_cast<StringLitExpr>(&Src);
    if (!Lit) return;
    const auto Len = static_cast<int64_t>(Lit->Value.size());

    // ISO §6.4.3.2: a string-type holds exactly n characters, so a literal has
    // to be exactly that long — too short is as wrong as too long, there being
    // no length to record a shorter one in.
    if (isCharStringType(Dst)) {
        const auto N = charStringLength(Dst);
        if (Len != N)
            error(Src.Loc, diag::err_string_length_mismatch,
                  {std::to_string(Len), std::to_string(N)});
        return;
    }

    // Turbo string[N]: a too-long literal TRUNCATES at run time
    // (plang_sstr_assign/plang_sstr_from_bytes -- runtime/plang_sstr.cpp)
    // rather than being an error the way EP's string(N) capacity below is --
    // ISO 10206 §6.9.2.2 is EP's own rule and was never Turbo's.  An
    // explicit early return rather than relying on the `!isVarStringLike`
    // return just below happening to also catch ShortString (it does,
    // isVarStringLike is false for ShortString by construction): this way
    // the "ShortString truncates, no diagnostic" decision is a fact this
    // function states about ShortString on its own terms, not a side effect
    // of a check meant for something else.
    if (isShortStringLike(&Dst)) return;
    if (!isVarStringLike(&Dst)) return;
    // The capacity lives on the string itself, which for `type s(n) = string(n);
    // var v: s(10)` is the schema's BODY and not the instance.  Widening the
    // test above without widening this read compared against the instance's
    // own StrCapacity of 0 and rejected `v := 'hi'` for not fitting a
    // string(0) -- the same half-widening this file's history keeps producing.
    const Type& DstStr = *schemaUnderlying(&Dst);
    // EP §6.4.7: a capacity fixed by a discriminant is not known here.  The
    // recorded one is the probe's, so comparing against it would reject
    // `p^.s := 'twelve chars'` for not fitting a string(1).  Whether it fits is
    // a run-time question, and codegen asks it against the capacity the object
    // carries.
    if (Dst.ExtentVaries || DstStr.ExtentVaries) return;
    if (Len > DstStr.StrCapacity)
        error(Src.Loc, diag::err_string_too_long,
              {std::to_string(Len), std::to_string(DstStr.StrCapacity)});
}

void Sema::checkIf(const IfStmt& S) {
    auto Cond = checkExpr(*S.Cond);
    if (!Cond->isError() && Cond->Kind != TypeKind::Boolean)
        error(S.Loc, diag::err_if_not_boolean, {Cond->Name});
    checkStmt(S.Then.get());
    if (S.Else) checkStmt(S.Else.get());
}

void Sema::checkWhile(const WhileStmt& S) {
    auto Cond = checkExpr(*S.Cond);
    if (!Cond->isError() && Cond->Kind != TypeKind::Boolean)
        error(S.Loc, diag::err_while_not_boolean, {Cond->Name});
    LoopScope LS(LoopDepth_);
    checkStmt(S.Body.get());
}

void Sema::checkFor(const ForStmt& S) {
    Symbol* Sym = Symtab.lookup(S.Var);
    if (!Sym) {
        error(S.Loc, diag::err_for_var_undefined, {S.Var});
    } else {
        // Driving a loop is a use: the control variable is named here, not in
        // an expression the identifier check would see.
        Sym->Referenced = true;

        // ISO §6.8.3.9: the control variable must be an entire variable. A
        // bare identifier can just as well resolve to a procedure, a builtin,
        // a constant, a type name, or a schema -- Ty is null for Proc/
        // Builtin/Schema (registerBuiltins and friends never set it), so
        // isOrdinal() below would null-deref, and for Const/TypeAlias/
        // EnumValue it is set but describes the wrong thing (the constant's
        // or alias's type, not any storage this loop could drive), which let
        // a const or type-alias control variable sail through Sema only to
        // hit "no storage" in codegen. VarParam is a variable too (isLValue
        // treats it the same as Var); it is excluded from a for-statement
        // only by not being LOCAL to this block, which the check below
        // already catches.
        const bool IsVar = Sym->Kind == SymbolKind::Var
                         || Sym->Kind == SymbolKind::VarParam;
        if (!IsVar) {
            error(S.Loc, diag::err_for_var_not_variable, {S.Var});
        } else {
            // See ForStmt::VarType's own comment (AstStmt.h): CodeGen needs
            // the control variable's declared type to pick a signed/unsigned
            // comparison for the loop condition, and Var (a bare name) has
            // nowhere else to carry it.  Set even where isOrdinal() is about
            // to fail below -- codegen never reaches a program Sema rejected,
            // and the field existing unconditionally here is simpler than
            // threading the same condition through twice.
            S.VarType = Sym->declaredType();
            if (!Sym->declaredType()->isOrdinal())
                error(S.Loc, diag::err_for_var_not_ordinal, {S.Var, Sym->declaredType()->Name});

            // ISO §6.8.3.9: the control variable must be local to the block
            // containing the for-statement.  A with-statement and a `for ... in`
            // open a scope that is not a block, so asking only the innermost one
            // rejected `with r do for i := 1 to 3 do ...` about an `i` that is
            // declared exactly where the standard requires.
            if (!Symtab.lookupInEnclosingBlock(S.Var))
                error(S.Loc, diag::err_for_var_not_local, {S.Var});
        }
    }
    auto From  = checkExpr(*S.From);
    auto Limit = checkExpr(*S.Limit);
    if (!From->isError() && !Limit->isError() && From->Kind != Limit->Kind
        && !(From->isNumeric() && Limit->isNumeric()))
        error(S.Loc, diag::err_for_bounds_incompatible, {From->Name, Limit->Name});

    // ISO §6.8.3.9: a bound's value is assigned to the control variable on
    // entry (and each iteration), so each one must be assignment-compatible
    // with ITS type too -- checking them only against each other above waived
    // through `for c := 1 to 10 do` for a char c (the ordinals print, not
    // 'a'..'j') and `for i := 1 to 10.5 do` (the real bound silently
    // truncates): two numeric types can agree with each other without either
    // agreeing with the variable both are being assigned into.  Skipped once
    // the variable itself is not ordinal (or unresolved): that is already
    // err_for_var_not_ordinal's diagnostic to give, not a second one here.
    // Sym->declaredType() is only meaningful (and only non-null) for a
    // Var/VarParam symbol -- see the comment above IsVar's declaration.  A
    // Builtin/Proc control variable already got err_for_var_not_variable
    // above and has a null declared type (isOrdinal() would null-deref); a
    // Const/TypeAlias/EnumValue one already got the same diagnostic and has
    // a non-null declared type describing the wrong thing.  Both are
    // excluded here the same way IsVar excludes them above, so this block
    // never dereferences a null declared type and never piles a second
    // diagnostic onto err_for_var_not_variable.
    const bool BoundsCheckApplies = Sym
        && (Sym->Kind == SymbolKind::Var || Sym->Kind == SymbolKind::VarParam)
        && Sym->declaredType()->isOrdinal();
    if (BoundsCheckApplies) {
        if (!From->isError() && !isAssignCompatible(*Sym->declaredType(), *From))
            error(S.From->Loc, diag::err_for_bound_wrong_type,
                  {From->Name, S.Var, Sym->declaredType()->Name});
        if (!Limit->isError() && !isAssignCompatible(*Sym->declaredType(), *Limit))
            error(S.Limit->Loc, diag::err_for_bound_wrong_type,
                  {Limit->Name, S.Var, Sym->declaredType()->Name});
    }

    // ISO §6.8.3.9: the body must not threaten the control variable (assign to it).
    checkForBody(S.Body.get(), S.Var, S.Loc);
    LoopScope LS(LoopDepth_);
    checkStmt(S.Body.get());
}

bool Sema::hidesName(const ProcDecl& P, const std::string& Name) {
    // The procedure's own identifier, and the result variable a function may
    // name, are as much in scope inside it as anything it declares.
    if (eqCI(P.Name, Name) || eqCI(P.ResultName, Name)) return true;
    for (const auto& Pg : P.heading().Params)
        for (const auto& Nm : Pg.Names)
            if (eqCI(Nm, Name)) return true;
    if (!P.Body) return false;
    for (const auto& C : P.Body->Consts) if (eqCI(C.Name, Name)) return true;
    for (const auto& T : P.Body->Types)  if (eqCI(T.Name, Name)) return true;
    for (const auto& Q : P.Body->Procs)  if (eqCI(Q->Name, Name)) return true;
    for (const auto& G : P.Body->Vars)
        for (const auto& Nm : G.Names)
            if (eqCI(Nm, Name)) return true;
    return false;
}

void Sema::checkProcForThreats(
        const ProcDecl& P, const std::string& VarName, SourceLocation ForLoc,
        const std::vector<std::unique_ptr<ProcDecl>>& Siblings) {
    // Nothing inside a procedure that redeclares the name can reach the control
    // variable, and that goes for the procedures nested within it too.
    if (hidesName(P, VarName) || !P.Body) return;

    if (P.Body->Body) {
        // Which procedure a call names is resolved from the declaration parts in
        // scope at the call, the symbol table for them having been closed.
        std::vector<const ProcDecl*> Callables;
        for (const auto& Q : Siblings)      Callables.push_back(Q.get());
        for (const auto& Q : P.Body->Procs) Callables.push_back(Q.get());
        checkForBody(P.Body->Body.get(), VarName, ForLoc, &Callables);
    }
    for (const auto& Q : P.Body->Procs)
        checkProcForThreats(*Q, VarName, ForLoc, P.Body->Procs);
}

/// Static type of a `with`-clause record expression; see the Sema.h doc
/// comment for why this does not simply call checkExpr.  Handles the forms
/// ISO §6.8.3.1's variable-access production allows in a with-record: a plain
/// name, a field selection, a pointer dereference, and an indexed component.
/// Anything else (a call, say) is out of that grammar and returns null.
std::shared_ptr<Type> Sema::quietWithRecordType(const ExprNode* E) {
    if (auto* Id = llvm::dyn_cast<IdentExpr>(E)) {
        Symbol* Sym = Symtab.lookup(Id->Name);
        return Sym ? Sym->declaredType() : nullptr;
    }
    if (auto* F = llvm::dyn_cast<FieldExpr>(E)) {
        auto RecTy = quietWithRecordType(F->Record.get());
        if (!RecTy) return nullptr;
        // EP §6.4.7: `v.d` on a schematic value is the discriminant when d
        // names one -- checkField's own precedence, mirrored here.
        if (RecTy->Kind == TypeKind::Schema || RecTy->Kind == TypeKind::SchemaInstance)
            for (const auto& D : RecTy->SchemaDiscs)
                if (eqCI(D.Name, F->Field)) return D.Ty;
        const Type* Body = schemaUnderlying(RecTy.get());
        const Type::Field* Fld = Body && Body->Kind == TypeKind::Record
                                ? Body->fieldByName(F->Field) : nullptr;
        return Fld ? Fld->Ty : nullptr;
    }
    if (auto* D = llvm::dyn_cast<DerefExpr>(E)) {
        auto PtrTy = quietWithRecordType(D->Pointer.get());
        return (PtrTy && PtrTy->Kind == TypeKind::Pointer) ? PtrTy->PointeeType : nullptr;
    }
    if (auto* Ix = llvm::dyn_cast<IndexExpr>(E)) {
        auto ArrTy = quietWithRecordType(Ix->Array.get());
        return (ArrTy && ArrTy->Kind == TypeKind::Array) ? ArrTy->ElemType : nullptr;
    }
    return nullptr;
}

bool Sema::withExposesName(const WithStmt& S, const std::string& Name) {
    for (const auto& Rec : S.Records) {
        auto T = quietWithRecordType(Rec.get());
        if (!T) continue;
        // EP §6.4.7: an undiscriminated or instantiated schema exposes its
        // discriminants alongside its body's fields (see pushWithScope).
        if (T->Kind == TypeKind::Schema || T->Kind == TypeKind::SchemaInstance)
            for (const auto& Dc : T->SchemaDiscs)
                if (eqCI(Dc.Name, Name)) return true;
        const Type* Body = schemaUnderlying(T.get());
        if (Body && Body->Kind == TypeKind::Record && Body->fieldByName(Name))
            return true;
    }
    return false;
}

// Unified for-loop threat scanner (ISO §6.8.3.9).
// When `Callables` is null the symbol table resolves var-param flags (intra-procedural).
// When `Callables` is non-null the symbol table scope is already closed; the supplied
// AST proc list is used instead (inter-procedural mode).
// `Shadowed` says whether an enclosing `with`, somewhere between the
// for-statement and `Stmt`, already exposes a field or discriminant spelled
// like `VarName` -- see withExposesName and the Sema.h doc comment.
void Sema::checkForBody(const StmtNode* Stmt, const std::string& VarName,
                         SourceLocation ForLoc,
                         const std::vector<const ProcDecl*>* Callables,
                         bool Shadowed) {
    if (!Stmt) return;
    const bool InterProc = Callables != nullptr;

    // A var-parameter actual threatens the control variable whether the call
    // stands alone as a statement (CallStmt) or sits inside an expression
    // (CallExpr) -- `y := f(i)` threatens exactly as a bare `f(i)` would.
    // Shared so both call forms resolve a callee's var-parameters identically,
    // in both intra- and inter-procedural mode.
    auto checkVarParamArgs = [&](const std::string& Name,
                                  const std::vector<std::unique_ptr<ExprNode>>& Args,
                                  SourceLocation Loc) {
        if (InterProc) {
            // Resolve var-param flags from AST proc list (symbol table scope is closed).
            for (const ProcDecl* Pd : *Callables) {
                if (!eqCI(Pd->Name, Name)) continue;
                size_t ArgIdx = 0;
                for (const auto& Pg : Pd->Params) {
                    for (size_t K = 0; K < Pg.Names.size(); ++K, ++ArgIdx) {
                        if (ArgIdx >= Args.size()) break;
                        if (!Pg.IsVar) continue;
                        if (auto* Id = llvm::dyn_cast<IdentExpr>(Args[ArgIdx].get()))
                            if (eqCI(Id->Name, VarName))
                                error(Loc, diag::err_for_inter_var_param, {VarName});
                    }
                }
                break;
            }
        } else {
            // Resolve var-param flags from the symbol table (intra-procedural).
            Symbol* Callee = Symtab.lookup(Name);
            if (Callee && (Callee->Kind == SymbolKind::Proc || Callee->Kind == SymbolKind::Builtin)) {
                for (size_t I = 0; I < Callee->Params.size() && I < Args.size(); ++I) {
                    if (!Callee->Params[I].IsVar) continue;
                    if (auto* Id = llvm::dyn_cast<IdentExpr>(Args[I].get()))
                        if (eqCI(Id->Name, VarName))
                            error(Loc, diag::err_for_body_var_param, {VarName});
                }
            }
        }
    };

    // A with-exposed field or discriminant spelled like VarName shadows it:
    // every check below is about the CONTROL VARIABLE specifically, and none
    // of them apply to a bare mention that with-scope resolution would send
    // somewhere else entirely (issue #291).
    if (!Shadowed) {
        if (auto* A = llvm::dyn_cast<AssignStmt>(Stmt)) {
            if (auto* Id = llvm::dyn_cast<IdentExpr>(A->Target.get()))
                if (eqCI(Id->Name, VarName))
                    error(A->Loc,
                          InterProc ? diag::err_for_inter_assigns
                                    : diag::err_for_body_assigns,
                          {VarName});
        } else if (!InterProc) {
            // Inner for-loop reuse: only checked in the direct (intra-procedural) scan.
            if (auto* Fs = llvm::dyn_cast<ForStmt>(Stmt))
                if (eqCI(Fs->Var, VarName))
                    error(Fs->Loc, diag::err_for_body_reuses, {VarName});
        }
        if (auto* Cs = llvm::dyn_cast<CallStmt>(Stmt)) {
            std::string Lo = toLower(Cs->Name);
            // EP §6.7.5.5: readstr(e, v1,...,vn) reads e and assigns every v --
            // the mirror of read/readln, except its first argument is the
            // SOURCE rather than the first destination.  writestr(e, ...) is
            // the other mirror, assigning only e.  Neither was recognized
            // here, so `for i := 1 to 3 do readstr(s, i)` passed unchallenged.
            auto isWriteArg = [&](size_t I) {
                if (Lo == "read" || Lo == "readln") return true;
                if (Lo == "readstr")                return I >= 1;
                if (Lo == "writestr")               return I == 0;
                return false;
            };
            for (size_t I = 0; I < Cs->Args.size(); ++I) {
                if (!isWriteArg(I)) continue;
                if (auto* Id = llvm::dyn_cast<IdentExpr>(Cs->Args[I].get()))
                    if (eqCI(Id->Name, VarName))
                        error(Cs->Loc,
                              InterProc ? diag::err_for_inter_reads
                                        : diag::err_for_body_reads,
                              {VarName});
            }
            checkVarParamArgs(Cs->Name, Cs->Args, Cs->Loc);
        }
        // A var-parameter call need not be a statement of its own: `y := f(i)`
        // threatens exactly as `f(i)` alone would, through an expression the
        // statement walk never used to look inside (issue #265).  Every
        // CallExpr hanging off this one statement, at any depth, is checked
        // the same way a top-level CallStmt already is above.
        forEachStmtExpr(Stmt, [&](const ExprNode* E) {
            walkExprs(E, [&](const ExprNode* Ex) {
                if (auto* Ce = llvm::dyn_cast<CallExpr>(Ex))
                    checkVarParamArgs(Ce->Name, Ce->Args, Ce->Loc);
            }, StackBaseline);
        });
    }

    // Recurse exactly where walkStmts (SemaUtil.h) would; unlike that shared
    // walk, a with-body's Shadowed state may differ from what it was on the
    // way in, which is why this is not simply `walkStmts(Stmt, ...)` above.
    if (auto* Cs = llvm::dyn_cast<CompoundStmt>(Stmt)) {
        for (const auto& St : Cs->Stmts)
            checkForBody(St.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Is = llvm::dyn_cast<IfStmt>(Stmt)) {
        checkForBody(Is->Then.get(), VarName, ForLoc, Callables, Shadowed);
        checkForBody(Is->Else.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Fs = llvm::dyn_cast<ForStmt>(Stmt)) {
        checkForBody(Fs->Body.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Fi = llvm::dyn_cast<ForInStmt>(Stmt)) {
        checkForBody(Fi->Body.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Ws = llvm::dyn_cast<WhileStmt>(Stmt)) {
        checkForBody(Ws->Body.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Rs = llvm::dyn_cast<RepeatStmt>(Stmt)) {
        for (const auto& St : Rs->Stmts)
            checkForBody(St.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Cas = llvm::dyn_cast<CaseStmt>(Stmt)) {
        for (const auto& Arm : Cas->Arms)
            checkForBody(Arm.Body.get(), VarName, ForLoc, Callables, Shadowed);
        checkForBody(Cas->Else.get(), VarName, ForLoc, Callables, Shadowed);
    } else if (auto* Wts = llvm::dyn_cast<WithStmt>(Stmt)) {
        checkForBody(Wts->Body.get(), VarName, ForLoc, Callables,
                     Shadowed || withExposesName(*Wts, VarName));
    } else if (auto* Ls = llvm::dyn_cast<LabeledStmt>(Stmt)) {
        checkForBody(Ls->Stmt.get(), VarName, ForLoc, Callables, Shadowed);
    }
}

void Sema::checkRepeat(const RepeatStmt& S) {
    warnUnreachable(S.Stmts);
    {
        LoopScope LS(LoopDepth_);
        for (const auto& St : S.Stmts) checkStmt(St.get());
    }
    auto Cond = checkExpr(*S.Cond);
    if (!Cond->isError() && Cond->Kind != TypeKind::Boolean)
        error(S.Cond->Loc, diag::err_repeat_not_boolean, {Cond->Name});
}

/// ISO §6.9.2: a read-parameter is a variable of an integer, real or char
/// type; EP adds a string type.  Anything else used to be accepted and handed
/// to whichever runtime reader its storage width selected.
///
/// A typed (non-text) file reads a whole component of its own type, whatever
/// that is, so this only constrains what is read from a textfile -- which is
/// every read whose first argument is not a typed file.
/// EP §6.7.3.1 / §6.11.2: writing through a protected parameter or a protected
/// imported variable, whatever access path is written on top of it.
///
/// This walked nested INDEX expressions only, so `arr[1] := 7` was caught and
/// `r.f := 5` was not -- a field selection reaches the same storage by a
/// different spelling.  And it was called from the assignment statement alone,
/// so the two other ways a program writes to a variable went unasked: passing
/// it as a var-parameter actual, and naming it as a read target.  Both modify
/// it as surely as an assignment does.
///
/// A dereference deliberately stops the walk: `p^ := x` through a protected
/// pointer modifies what p points AT, not p, and §6.7.3.1 protects the
/// parameter rather than the object it reaches.
Symbol* Sema::protectedBaseOf(const ExprNode& Target) {
    const ExprNode* Base = &Target;
    for (;;) {
        if (auto* Ix = llvm::dyn_cast<IndexExpr>(Base)) { Base = Ix->Array.get(); continue; }
        if (auto* Fe = llvm::dyn_cast<FieldExpr>(Base)) { Base = Fe->Record.get(); continue; }
        // TP-only, issue #711: TypeName(expr) used as a variable does not
        // convert anything -- it reinterprets the OPERAND's own storage in
        // place (isLValue's own comment, above in SemaExpr.cpp, has the full
        // rule and the same-size requirement that makes it an lvalue at
        // all). A write through the cast is therefore a write through
        // whatever the operand itself traces back to, exactly the way a
        // FieldExpr or IndexExpr reaches the same storage by a different
        // spelling just above -- `rec2(r).c := 0` on a `const r: rec` was
        // walking off the end of this loop at the TypeCastExpr node (it
        // matched neither arm) and returning null, so the const/protected
        // check below was skipped entirely rather than asked about `r`.
        if (auto* Tc = llvm::dyn_cast<TypeCastExpr>(Base)) { Base = Tc->Operand.get(); continue; }
        break;
    }
    auto* Id = llvm::dyn_cast<IdentExpr>(Base);
    if (!Id) return nullptr;
    Symbol* Sym = Symtab.lookup(Id->Name);
    // EP §6.7.3.7.1 NOTE 2: a conformant-array bound identifier is refused an
    // assignment the same way a protected parameter is -- see
    // checkNotProtected, which tells the two apart for the diagnostic.
    // Turbo's own `const` parameter (IsConstParam) is refused the same way
    // again, for its own reason: see ParamGroup::IsConst's own comment
    // (AstType.h) for why it is a separate flag from IsProtected rather than
    // folded into it.
    return (Sym && (Sym->IsProtected || Sym->IsConformantBound || Sym->IsConstParam))
        ? Sym : nullptr;
}

void Sema::checkNotProtected(const ExprNode& Target, SourceLocation Loc) {
    Symbol* Sym = protectedBaseOf(Target);
    if (!Sym) return;
    if (Sym->IsConformantBound) {
        error(Loc, diag::err_conformant_bound_assigned, {Sym->Name});
        return;
    }
    if (Sym->IsConstParam) {
        error(Loc, diag::err_const_param_assigned, {Sym->Name});
        return;
    }
    // EP §6.11.2: a variable exported 'protected' is protected in the same way,
    // but for a different reason, and saying "parameter" about a module's
    // variable would only mislead.
    error(Loc, Sym->Module.empty() ? diag::err_protected_param_assigned
                                   : diag::err_protected_import_assigned,
          {Sym->Name});
}

void Sema::checkReadParamType(const Type& T, SourceLocation Loc) {
    const Type* Base = &T;
    while (Base->Kind == TypeKind::Subrange && Base->SubBase)
        Base = Base->SubBase.get();
    switch (Base->Kind) {
    case TypeKind::Integer:
    case TypeKind::Real:
    case TypeKind::Char:
    case TypeKind::String:
    case TypeKind::VarString:
    // Turbo string[N]: CodeGen (BuiltinIO.cpp's emitReadArg) has a minimal
    // read implementation for it, the same basic capability VarString gets
    // here -- see that file's own comment for exactly what it does and does
    // not yet do (TP-exact truncation semantics are a later item).
    case TypeKind::ShortString:
        return;
    default:
        // ISO §6.10.1(e): a fixed-string-type (packed array[1..n] of char) is
        // as legal a read-parameter as EP's variable-string-type is -- the
        // write side (checkCallStmt's writeln/write arm) already knew this
        // and the read side did not, so `readln(c)` for a `packed array of
        // char` was refused though `writeln(c)` already worked.  Same for a
        // schema whose body is a string, looked through however many levels
        // of nested instantiation (EP §6.4.7) it takes to reach one.
        if (isCharStringType(*Base)) return;
        if ((Base->Kind == TypeKind::Schema || Base->Kind == TypeKind::SchemaInstance)
                && Base->SchemaBody
                && schemaUnderlying(Base->SchemaBody)->Kind == TypeKind::VarString)
            return;
        error(Loc, diag::err_read_param_type, {T.Name});
    }
}

void Sema::checkCallStmt(const CallStmt& S) {
    Symbol* Sym = Symtab.lookup(S.Name);
    if (!Sym) {
        // Several of Turbo's real-mode DOS names -- Mark, Release, Keep,
        // Intr, MsDos, GetIntVec, SetIntVec -- are ordinarily called as
        // procedure STATEMENTS, never used as expression values, so this
        // needs its own check: checkIdent's (SemaExpr.cpp) never runs for
        // `Intr($21, Regs);` written as a bare statement.
        if (checkRealModeDosName(S.Name, S.Loc)) return;
        // Turbo Tier 5, issues #571/#623: an unqualified name with no
        // ordinary declaration anywhere in scope may still be one of the
        // CURRENT implicit receiver's own methods -- 'Who;' inside
        // 'procedure TA.Test1' meaning 'Self.Who;' (#571), or 'Speak;'
        // inside 'with d do' meaning 'd.Speak;' (#623).  See
        // checkImplicitMethodCallStmt's own comment (Sema.h) for why this is
        // tried only here, after an ordinary lookup already failed.
        if (checkImplicitMethodCallStmt(S)) return;
        error(S.Loc, diag::err_undefined_procedure, {S.Name});
        return;
    }
    if (Sym->Kind == SymbolKind::Builtin) {
        S.ResolvedBuiltin = Sym->BuiltinKind;
        std::string Lo = toLower(S.Name);

        // ISO §6.8.2.3: a procedure-statement names a procedure.  A required
        // function written as one reached codegen, which has no procedure of
        // that name to call and made one up: a call to a plang_abs that no
        // runtime defines, or one whose arguments were not what it takes.
        //
        // Turbo `{$X+}` (its default) lifts that requirement, the same as
        // checkUserDefinedCall's identical gate does for a user-declared
        // function just below -- falling through here reaches the ordinary
        // builtin dispatch chain below with S.Args still to be type-checked
        // and no `lo ==` arm of its own to match (no required FUNCTION
        // shares a name with a required procedure), landing on the generic
        // "evaluate for side effects, no result wanted" fallback at the end
        // of this function.
        if (Sym->IsFunction) {
            // Opts.turbo() is not redundant with switchOn -- see
            // checkUserDefinedCall's identical gate (SemaExpr.cpp) for why.
            if (!(Opts.turbo() && Opts.switchOn(Switch::ExtendedSyntax, S.Loc))) {
                error(S.Loc, diag::err_func_as_statement, {S.Name});
                for (const auto& Arg : S.Args) (void)checkExpr(*Arg);
                return;
            }
            S.ResolvedType = Sym->ReturnType;
        }

        if (!checkEPOnly(*Sym, S.Loc)) {
            for (const auto& Arg : S.Args) (void)checkExpr(*Arg);
            return;
        }

        // checkCallExpr (SemaExpr.cpp) asks this before dispatching on the
        // builtin's spelling; this arm did not, so a call that is not one of
        // the special cases below -- reset, close, dispose, ... -- reached
        // codegen with whatever argument count was written.  `reset(f, 1, 2)`
        // let its extra argument through to a runtime call already typed for
        // two, which the IR verifier rejected as a compiler crash rather than
        // a diagnostic; `reset()` reached emitUserProcCall and failed to link
        // against an external symbol nothing defines.  Builtins.def's arity is
        // the same source checkBuiltinArity already reads for expressions.
        if (!checkBuiltinArity(Sym->BuiltinKind, Lo, S.Loc, S.Args.size())) {
            for (const auto& Arg : S.Args) (void)checkExpr(*Arg);
            return;
        }

        // TP-only: Assigned(p) called as a statement under `{$X+}` (its
        // result discarded) -- same shape check as checkCallExpr's own arm,
        // duplicated here because checkCallExpr is never reached for a
        // builtin called this way (mirrors eof/eoln/position/... below,
        // which have the identical split for the same reason).  Issue #649:
        // same unconditional ProcVarRawValue marking as checkCallExpr's own
        // arm (SemaExpr.cpp) -- see its comment there.
        if (Lo == "assigned" && !S.Args.empty()) {
            if (auto* Id = llvm::dyn_cast<IdentExpr>(S.Args[0].get());
                    Id && isProcVarNameCandidate(*Id))
                Id->Resolution = IdentExpr::IdentResolution::ProcVarRawValue;
            auto ArgTy = checkExpr(*S.Args[0]);
            if (!ArgTy->isError() && ArgTy->Kind != TypeKind::Pointer
                    && !isCallable(*ArgTy))
                error(S.Args[0]->Loc, diag::err_assigned_argument, {Lo, ArgTy->Name});
            return;
        }

        // §6.9.4: page, like readln and writeln, is about lines, and only a
        // text file has any.  The other two are checked where their arguments
        // are already being walked, since checking an expression twice reports
        // anything wrong with it twice.
        if (Lo == "page" && !S.Args.empty()) {
            auto T = checkExpr(*S.Args[0]);
            // isTextFile (Type.h) is dialect-aware: under -std=turbo a
            // `file of char` is a binary file, not text, so `page` on one is
            // refused exactly like `page` on `file of integer` always has
            // been -- and a genuinely untyped `file` (also null ElemType,
            // like `text`) is now correctly refused too instead of silently
            // passing as if it were text (see Type.h's isTextFile comment).
            if (T->Kind == TypeKind::File && !isTextFile(*T, Opts))
                error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                      {Lo, T->Name});
            return;
        }

        // EP §6.7.5.2: reset/rewrite/extend/update's FIRST argument is
        // always the file variable to (re)open, the same arg0-must-be-a-
        // file requirement Assign's and Seek's own arms below already
        // enforce.  Until now nothing checked it at all -- both arms below
        // used to do a bare `(void)checkExpr(*S.Args[0])`, discarding the
        // type -- so CodeGen's FileVarHelpers::fileVarPtr's IdentExpr fast
        // path (its own comment) handed back ANY variable's storage address
        // with no type check of its own: `reset(i)` for a plain integer i
        // reached the runtime with i's own storage reinterpreted as a
        // PascalFile*, corrupting memory / segfaulting with no diagnostic --
        // the same bug class issue #417 closed for
        // position/lastposition/empty and issue #306's fourth slice closed
        // for get/put/close (issue #557).  Checked once here, ahead of the
        // optional second-argument arms below, so `reset(f)`/`rewrite(f)`/
        // `extend(f)`/`update(f)` (arg1 omitted -- legal per Builtins.def's
        // MinArgs = 1) get the same check a second argument's presence used
        // to gate entirely.
        if ((Lo == "reset" || Lo == "rewrite" || Lo == "extend" || Lo == "update")
                && !S.Args.empty()) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError() && FTy->Kind != TypeKind::File)
                error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});

            // reset/rewrite/extend/update's optional second argument names
            // the external file, and CodeGen's marshalling
            // (StringCallMarshalling::emitCStrArg) only has cases for a char
            // value, an EP string(n), and a plain ISO 7185 String -- every
            // one of those turns into a `const char *` cleanly.  Nothing
            // checked the argument's type before it reached codegen, so
            // `reset(f, 42)` (or a Boolean, a record, an array, ...) arrived
            // at emitCStrArg shaped nothing like a string and made LLVM's
            // own IR verifier abort the whole compiler with an internal
            // error instead of plang ever refusing the program.
            // isCharStringType (a declared `packed array[1..n] of char`) and
            // Turbo's ShortString/PChar are deliberately NOT accepted here
            // even though they read as string-like: emitCStrArg has no
            // marshalling case for any of them either (confirmed
            // empirically -- a ShortString actual compiles but silently
            // opens the wrong file, no diagnostic), so admitting them would
            // just move the crash/silent-corruption to a different actual
            // rather than remove it.  Widening this accept-list is separate,
            // larger work (teaching emitCStrArg the missing cases), not this
            // fix.
            // -std=turbo: real Turbo Pascal's Reset/Rewrite second argument
            // is an INTEGER RecSize for an untyped file (confirmed against
            // `fpc -Mtp`: `reset(f, 'x.txt')` is REJECTED there with an
            // incompatible-type error), the mirror image of EP's own
            // reset(f, name) external-file-name form just below.  This
            // project used to accept a string here under Turbo too (PR
            // #475/#478, kept as an implicit Assign, purely a plang-only
            // convenience) -- now that Assign exists as its own explicit
            // builtin, that convenience contradicts real field practice and
            // is retired: a genuine Turbo program writes `Assign(f,
            // 'name'); Reset(f);`, never `Reset(f, 'name')`.  Extend/Update
            // are EP-only builtins (Builtins.def), so they never reach this
            // arm under Turbo and keep taking a filename unconditionally.
            if (Opts.turbo() && (Lo == "reset" || Lo == "rewrite")
                    && S.Args.size() == 2) {
                auto SizeTy = checkExpr(*S.Args[1]);
                if (!SizeTy->isError() && SizeTy->Kind != TypeKind::Integer)
                    error(S.Args[1]->Loc, diag::err_turbo_reset_rewrite_recsize_type,
                          {Lo, SizeTy->Name});
                return;
            }

            if (S.Args.size() == 2) {
                auto NameTy = checkExpr(*S.Args[1]);
                if (!NameTy->isError() && NameTy->Kind != TypeKind::Char
                        && NameTy->Kind != TypeKind::String
                        && !isVarStringLike(NameTy.get()))
                    error(S.Args[1]->Loc, diag::err_reset_rewrite_arg_type,
                          {Lo, NameTy->Name});
            }
            return;
        }

        // EP §6.7.5.2: SeekRead(f, n) / SeekWrite(f, n) / SeekUpdate(f, n)
        // -- f is a direct-access file the same arg0-must-be-a-file shape
        // Seek's own arm below already enforces, and n is a value of f's
        // declared index type (CGProcCall::emitCallStmt's own comment on
        // this identical shape).  Until now these three had NO Sema arm at
        // all, hand-written or generic -- Builtins.def's AK_Any left them
        // falling through checkCallStmt's generic per-argument checkExpr
        // fallback with no file-kind check, so `seekread(i, 1)` for a plain
        // integer i reached CodeGen's FileVarHelpers::fileVarPtr the same
        // unchecked way reset/rewrite/extend/update above did (issue #557).
        // Stays hand-written rather than moving to AK_File: arg1 (n) is a
        // DIFFERENT kind (Integer) from arg0's File, the same multi-kind
        // shape Seek's own arm keeps hand-written for.
        if ((Lo == "seekread" || Lo == "seekwrite" || Lo == "seekupdate")
                && S.Args.size() == 2) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError() && FTy->Kind != TypeKind::File)
                error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
            auto NTy = checkExpr(*S.Args[1]);
            if (!NTy->isError() && !NTy->isIntegral())
                error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, NTy->Name});
            return;
        }

        // write() with no arguments is an error (ISO §6.9.3).
        if (Lo == "write" && S.Args.empty()) {
            error(S.Loc, diag::err_write_requires_args);
            return;
        }

        // ISO §6.9.1: on a file that is not a textfile, write(f,e) means
        // f^ := e, so a component of any type at all may be written and the
        // rule is assignment compatibility.  A file of char stays on the
        // textfile path, where plang formats what it is given.
        if (Lo == "write" && S.Args.size() >= 2) {
            auto T = checkExpr(*S.Args[0]);
            if (T->Kind == TypeKind::File && !isTextFile(*T, Opts)) {
                // -std=turbo: an untyped `file` has no component type to
                // write, and real Turbo Pascal has no plain Write on one at
                // all -- only BlockWrite (not yet implemented) transfers an
                // untyped file's bytes.  Confirmed against `fpc -Mtp`.
                // Without this, T->ElemType below is null and every
                // *T->ElemType dereference just past it is undefined
                // behavior -- this is a genuine crash fix, not just a nicer
                // diagnostic, now that isTextFile (unlike the old inline
                // `T->ElemType &&` guard) no longer excludes an untyped file
                // from this branch.
                if (Opts.turbo() && !T->ElemType) {
                    error(S.Loc, diag::err_turbo_untyped_file_read_write, {Lo});
                    for (size_t I = 1; I < S.Args.size(); ++I) (void)checkExpr(*S.Args[I]);
                    return;
                }
                if (!T->ElemType) return; // untyped file, non-Turbo: unreachable today (no ISO/EP untyped file), but stay safe.
                for (size_t I = 1; I < S.Args.size(); ++I) {
                    auto At = checkExpr(*S.Args[I]);
                    if (At->isError() || T->ElemType->isError()) continue;
                    // -std=turbo: exact type identity, no implicit
                    // assignment-compatible widening -- see
                    // err_turbo_typed_file_exact_type's own comment.
                    if (Opts.turbo()) {
                        if (!isIdenticalType(T->ElemType, At))
                            error(S.Args[I]->Loc, diag::err_turbo_typed_file_exact_type,
                                  {Lo, T->ElemType->Name, At->Name});
                        continue;
                    }
                    if (!isAssignCompatible(*T->ElemType, *At))
                        error(S.Args[I]->Loc, diag::err_write_type_mismatch,
                              {T->ElemType->Name, At->Name});
                }
                return;
            }
        }

        // ISO §6.9.3.1: a write-parameter to a textfile is an integer, real,
        // boolean, char or string value.  Anything else reaches codegen with
        // no writer to dispatch to, so it has to be turned away here.
        if (Lo == "write" || Lo == "writeln") {
            for (size_t I = 0; I < S.Args.size(); ++I) {
                const auto* Arg = S.Args[I].get();
                if (auto* Wp = llvm::dyn_cast<WriteParam>(Arg)) Arg = Wp->Value.get();
                auto T = checkExpr(*S.Args[I]);
                if (T->isError()) continue;
                // The first argument may name the destination file instead of
                // being a value to write.
                if (I == 0 && T->Kind == TypeKind::File) {
                    // §6.9.5: writeln ends a line, and only a text file has
                    // lines to end.  isTextFile (Type.h) is dialect-aware --
                    // see its comment for why a `file of char` is text under
                    // ISO/EP but not Turbo, and why an untyped `file` is
                    // correctly refused here too now instead of silently
                    // passing.
                    if (Lo == "writeln" && !isTextFile(*T, Opts))
                        error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                              {Lo, T->Name});
                    continue;
                }
                // EP §6.4.2.5: a restricted value is not one of the types
                // §6.9.3.1 admits, whatever it restricts — writing it would
                // show the representation the restriction is there to hide.
                if (T->isRestricted()) {
                    error(S.Args[I]->Loc, diag::err_restricted_used, {T->Name});
                    continue;
                }
                switch (T->Kind) {
                case TypeKind::Integer: case TypeKind::Real:
                case TypeKind::Boolean: case TypeKind::Char:
                case TypeKind::String:  case TypeKind::VarString:
                case TypeKind::Subrange: case TypeKind::Enum:
                case TypeKind::Complex: // EP §6.9.3.6
                // Turbo string[N]: CodeGen (BuiltinIO.cpp's emitWriteArgs) has
                // a minimal write implementation, the same basic capability
                // VarString gets here -- see that file's own comment for the
                // exact scope (not yet TP-exact truncation/field-width edge
                // cases beyond what is already implemented).
                case TypeKind::ShortString:
                    break;
                default:
                    // ISO §6.4.3.2: a packed array[1..n] of char is a string
                    // value, and §6.9.3.1 admits string values.
                    if (isCharStringType(*T)) break;
                    // Turbo PChar/PAnsiChar (and any `^Char`-pointee type
                    // under -std=turbo, isCharPointerType's own structural
                    // rule): a null-terminated C string, written the same
                    // way a var-string is.  Checked directly against
                    // `fpc -Mtp`: `writeln(p)`/`write(p:w)` print the bytes p
                    // points to up to the terminator, exactly what CodeGen's
                    // emitWriteValue already does for any bare pointer value
                    // that reaches its string-writer fallback -- this was
                    // the only thing standing between a PChar and codegen
                    // already handling it.  Gated on Opts.turbo() the same
                    // way every other isCharPointerType caller is: an ISO/EP
                    // `^char` is an ordinary pointer, not a C string, and
                    // must not become writable just because its pointee
                    // happens to be Char.
                    if (Opts.turbo() && isCharPointerType(*T)) break;
                    // EP §6.4.3.3 makes `string` a schema, so a schema whose
                    // BODY is a string denotes a string value as surely as
                    // `string(10)` does: `type s(n: integer) = string(n);
                    // var v: s(10)` was refused here for not being one.  The
                    // body may itself be another schema instantiation, so
                    // this is asked of schemaUnderlying, not the immediate
                    // hop -- `type a(m)=string(m); b(n)=a(n); var v: b(10)`
                    // was refused the same way for the identical reason.
                    if ((T->Kind == TypeKind::Schema
                         || T->Kind == TypeKind::SchemaInstance)
                            && T->SchemaBody
                            && schemaUnderlying(T->SchemaBody)->Kind == TypeKind::VarString)
                        break;
                    error(Arg->Loc, diag::err_write_param_type, {T->Name});
                }
            }
            return;
        }

        // readln: first arg, if a file, must be a text file.
        // EP §6.4.2.5: a restricted variable is none of the types §6.9.2 reads
        // into, and giving it a value read from a file would be building one
        // out of the representation the restriction hides.
        if (Lo == "read" || Lo == "readln") {
            // Every argument is checked once and its type kept: checking an
            // expression twice reports anything wrong with it twice, and the
            // first argument's type is not known until it has been checked.
            std::vector<std::shared_ptr<Type>> Ts;
            Ts.reserve(S.Args.size());
            for (const auto& Arg : S.Args) Ts.push_back(checkExpr(*Arg));

            // §6.9.2 reads into an integer, a real or a char variable, and EP
            // adds a string -- but that is the rule for a TEXTFILE.  §6.9.1's
            // read on a typed file reads a whole component, of whatever type
            // that file has, and err_read_type_mismatch is what checks that.
            // A file as the first argument names where to read from rather
            // than what to read into.
            const bool HasFile  = !Ts.empty() && Ts[0]->Kind == TypeKind::File;
            // isTextFile (Type.h) is dialect-aware -- see its comment.  It
            // also fixes a real bug here: the old `!Ts[0]->ElemType` half of
            // this test made a genuinely untyped `file` (ElemType null,
            // exactly like `text`'s) silently take the textfile read path
            // too.  isTextFile tells the two apart by name, so an untyped
            // file no longer reads FromText == true.
            const bool FromText = !HasFile || isTextFile(*Ts[0], Opts);

            // -std=turbo: real Turbo Pascal has no plain Read on an untyped
            // file at all -- only BlockRead (not yet implemented) transfers
            // its bytes.  Confirmed against `fpc -Mtp`
            // ("Can't use read or write on untyped file").  Without this,
            // an untyped file's `read(f, v)` would fall neither into the
            // FromText path above (now false) nor the typed-component path
            // below (T->ElemType is null there too), so it would silently
            // compile to nothing meaningful rather than being refused.
            if (Lo == "read" && HasFile && Opts.turbo() && Ts[0]->Kind == TypeKind::File
                    && !Ts[0]->ElemType && !Ts[0]->isPredefinedText()) {
                error(S.Loc, diag::err_turbo_untyped_file_read_write, {Lo});
                return;
            }

            for (size_t I = 0; I < Ts.size(); ++I) {
                const auto& T = *Ts[I];
                if (T.isRestricted())
                    error(S.Args[I]->Loc, diag::err_restricted_used, {T.Name});
                else if (!(HasFile && I == 0) && FromText && !T.isError())
                    checkReadParamType(T, S.Args[I]->Loc);
                // ISO §6.9.1: read(v) is defined as v := f^, so a read target
                // must be a variable-access, the same requirement checkAssign
                // enforces on an ordinary assignment's left-hand side via
                // isLValue.  This was never asked here, so `read(5)`,
                // `read(someConst)` and `read(someFunc())` all reached
                // emitReadArg (CodeGen/BuiltinIO.cpp) with no address to
                // store into: some aborted the compiler outright
                // (codegenICE("read/readln target is not an assignable
                // variable")), others silently read into a throwaway
                // temporary nothing could ever see.  Gated the same way as
                // checkReadParamType and checkNotProtected: I == 0 is the
                // file itself when HasFile, not something read INTO, and a
                // T that is already an error type has been diagnosed once
                // and should not be piled onto here.
                if (!(HasFile && I == 0) && !T.isError() && !isLValue(*S.Args[I]))
                    error(S.Args[I]->Loc, diag::err_read_not_variable, {T.Name});
                // read(v) assigns to v -- §6.9.1 makes it `v := f^` -- so a
                // protected variable may no more be read into than assigned
                // to.  Gated the same way as checkReadParamType above: I == 0
                // is the file itself when HasFile, not something read INTO,
                // so checking whether the file variable is "protected" would
                // ask the wrong question -- a protected file argument to
                // read(f, v) was refused with err_protected_param_assigned,
                // a diagnostic about assigning through v that had nothing to
                // do with f.
                if (!(HasFile && I == 0))
                    checkNotProtected(*S.Args[I], S.Args[I]->Loc);
            }
        }

        // §6.9.5 and §6.9.4: readln, writeln and page apply to a textfile, and
        // so does eoln (§6.6.6.5) — they are all about lines, and only a text
        // file has any.  Only readln was being checked, so `writeln(f)` on a
        // file of integer wrote a newline into the middle of the components.
        // §6.9.5: readln reads past a line marker, which only a text file has.
        // The loop above has already checked every argument, so the type is
        // taken from the first rather than asked for a second time.
        if (Lo == "readln") {
            if (!S.Args.empty()) {
                const auto& T = S.Args[0]->ResolvedType;
                // isTextFile (Type.h) is dialect-aware -- see its comment
                // for why this also now refuses readln on an untyped file
                // (previously silently accepted, ElemType null like text's)
                // and on `file of char` under -std=turbo (now binary).
                if (T && T->Kind == TypeKind::File && !isTextFile(*T, Opts))
                    error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                          {Lo, T->Name});
            }
            return;
        }

        // read: if first arg is a typed file, remaining args must match element type.
        // Every argument was already checked once by the loop above, which set
        // each S.Args[I]->ResolvedType as a side effect (checkExpr, SemaExpr.cpp):
        // calling checkExpr on them again here reported anything wrong with an
        // argument -- e.g. an undefined identifier -- a second time (issue #272).
        // Read the cached type instead, the same way the readln arm just above
        // already does for S.Args[0].  The untyped-file-under-Turbo case
        // (T->ElemType null, not `text`) already returned above (the loop's
        // own err_turbo_untyped_file_read_write check) and never reaches
        // here.
        if (Lo == "read" && !S.Args.empty()) {
            const auto& T = S.Args[0]->ResolvedType;
            if (T && T->Kind == TypeKind::File && T->ElemType && S.Args.size() >= 2) {
                for (size_t I = 1; I < S.Args.size(); ++I) {
                    const auto& At = S.Args[I]->ResolvedType;
                    if (!At || At->isError() || T->ElemType->isError()) continue;
                    // -std=turbo: exact type identity for typed-file
                    // Read/Write, no implicit assignment-compatible
                    // widening -- see err_turbo_typed_file_exact_type.
                    if (Opts.turbo()) {
                        if (!isIdenticalType(T->ElemType, At))
                            error(S.Args[I]->Loc, diag::err_turbo_typed_file_exact_type,
                                  {Lo, T->ElemType->Name, At->Name});
                        continue;
                    }
                    if (!isAssignCompatible(*At, *T->ElemType))
                        error(S.Args[I]->Loc, diag::err_read_type_mismatch,
                              {T->ElemType->Name, At->Name});
                }
            }
            return;
        }

        // pack(a, i, z) and unpack(z, a, i).  The two are the same check with
        // the arguments in a different order: `a` is the unpacked array, `z`
        // the packed one and `i` where in `a` the transfer starts.
        //
        // ISO §6.6.5.4 asks only that `a` be an array, and a conformant array
        // parameter is one — its bounds are values rather than numbers, which
        // is a matter for the code generator and not for this.  Neither
        // operand was checked to be an array at all before, so `a` being
        // anything else reached a generator that had nothing to lower and
        // stopped the compiler.
        if ((Lo == "pack" || Lo == "unpack") && S.Args.size() == 3) {
            const bool IsPack = Lo == "pack";
            auto ArrTy = checkExpr(IsPack ? *S.Args[0] : *S.Args[1]);
            auto PkdTy = checkExpr(IsPack ? *S.Args[2] : *S.Args[0]);
            auto IdxTy = checkExpr(IsPack ? *S.Args[1] : *S.Args[2]);
            const ExprNode& ArrArg = IsPack ? *S.Args[0] : *S.Args[1];
            const ExprNode& PkdArg = IsPack ? *S.Args[2] : *S.Args[0];
            const ExprNode& IdxArg = IsPack ? *S.Args[1] : *S.Args[2];

            const bool ArrOk = ArrTy->Kind == TypeKind::Array
                            || ArrTy->Kind == TypeKind::ConformantArray;
            if (!ArrTy->isError() && !ArrOk)
                error(ArrArg.Loc, diag::err_pack_operand_not_array_unpacked,
                      {Lo, ArrTy->Name});
            const bool PkdOk = PkdTy->Kind == TypeKind::Array;
            if (!PkdTy->isError() && !PkdOk)
                error(PkdArg.Loc, diag::err_pack_operand_not_array_packed,
                      {Lo, PkdTy->Name});

            // ISO §6.6.5.4: pack/unpack is the one place an array crosses
            // between packed and unpacked, and the direction is fixed -- `a`
            // unpacked, `z` packed.  Neither Packed flag was looked at, so a
            // packed `a` or an unpacked `z` was accepted and copied exactly
            // as if the direction had been the one actually declared.
            if (ArrOk && ArrTy->Packed)
                error(ArrArg.Loc, diag::err_pack_operand_not_unpacked,
                      {Lo, ArrTy->Name});
            if (PkdOk && !PkdTy->Packed)
                error(PkdArg.Loc, diag::err_pack_operand_not_packed,
                      {Lo, PkdTy->Name});

            // ISO §6.6.5.4: the component types of a and z shall be
            // identical.  CGPackUnpack lowers the whole transfer as a single
            // memcpy sized and aligned from `a`'s element type alone, so a
            // mismatch here is not a value that fails to convert -- it is
            // bytes moved under the wrong element's size, e.g. an
            // `array of integer` packed into a `packed array of char`
            // copying 4 bytes per character instead of 1.  isAssignCompatible
            // permits integer -> real widening that a raw copy cannot
            // perform, so it is the wrong tool here; isIdenticalType is what
            // congruousConformant uses for the same "no conversion happens"
            // reason.
            if (ArrOk && PkdOk && ArrTy->ElemType && PkdTy->ElemType
                && !isIdenticalType(ArrTy->ElemType, PkdTy->ElemType))
                error(ArrArg.Loc, diag::err_pack_element_type_mismatch,
                      {Lo, ArrTy->ElemType->Name, PkdTy->ElemType->Name});

            // The index type of a conformant array is the ordinal its bounds
            // were declared with, which is as much a type as a written range.
            if (ArrOk && ArrTy->IndexType && !IdxTy->isError()
                && !isAssignCompatible(*ArrTy->IndexType, *IdxTy))
                error(IdxArg.Loc, IsPack ? diag::err_pack_index_mismatch
                                         : diag::err_unpack_index_mismatch,
                      {IdxTy->Name, ArrTy->IndexType->Name});
            return;
        }

        // EP §6.7.5.3: new(p, d1..ds) where p's domain-type is a schema-name.
        // The discriminants need not be constant, but there must be one per
        // formal discriminant and each must be ordinal.
        if (Lo == "new" && !S.Args.empty()) {
            auto PtrTy = checkExpr(*S.Args[0]);
            // Every check below -- schema discriminants, variant/schema extra
            // arguments -- is keyed off Pointee, which only a genuine Pointer
            // arg0 computes; anything else left it null and every one of
            // those checks silently no-opped rather than rejecting the call,
            // so `new(i)` for a non-pointer i sailed through Sema and reached
            // CodeGen with no pointee to size an allocation from.
            if (!PtrTy->isError() && PtrTy->Kind != TypeKind::Pointer)
                error(S.Args[0]->Loc, diag::err_new_arg_not_pointer, {PtrTy->Name});
            const Type* Pointee = PtrTy->Kind == TypeKind::Pointer
                                      ? PtrTy->PointeeType.get() : nullptr;
            const bool ToSchema = Pointee && Pointee->Kind == TypeKind::Schema;
            // ISO §6.6.5.3: for a record with a variant part, new's extra
            // arguments are its case-constants -- one per nested variant
            // level actually reached, each a value of that level's own tag
            // type -- not schema discriminants.  Vp is walked one level
            // deeper per argument by checkVariantTagArg; once it runs out
            // (the reached level's case-constants nest no further, or an
            // argument did not name any of them), ExtraReported flags that
            // every argument from there on is one too many.  This used to be
            // entirely unchecked: neither the count nor the type of these
            // arguments was validated for a variant record.
            const bool ToVariant = !ToSchema && Pointee && Pointee->Kind == TypeKind::Record
                                    && Pointee->RecordDecl && Pointee->RecordDecl->Variant;
            // Turbo Tier 5, Cluster A item 6: new(p, Ctor[(args)]) -- see
            // checkNewInit's own comment (Sema.h) for the whole design.
            // Gated on S.Args.size() > 1: a bare 'new(p)' for an object
            // pointee is unaffected (confirmed legal against fpc -Mtp,
            // allocating with no constructor run at all).
            if (!ToSchema && !ToVariant && Pointee && Pointee->Kind == TypeKind::Object
                    && S.Args.size() > 1) {
                checkNewInit(S, *Pointee);
                return;
            }
            const VariantPart* Vp = ToVariant ? Pointee->RecordDecl->Variant.get() : nullptr;
            bool ExtraReported = false;
            for (size_t I = 1; I < S.Args.size(); ++I) {
                auto At = checkExpr(*S.Args[I]);
                if (ToSchema && !At->isError() && I - 1 < Pointee->SchemaDiscs.size()) {
                    const auto& Disc = Pointee->SchemaDiscs[I - 1];
                    // The argument must be ordinal at all -- and, beyond that,
                    // assignment-compatible with THIS discriminant's own
                    // declared type, the same rule an ordinary assignment to
                    // it enforces.  Without the second check, `new(b, 42)` for
                    // `Box(c: boolean)` passed isOrdinal() on plain `integer`
                    // and reached codegen, which truncated 42 to its low bit
                    // and stored it -- a silent wrong answer where an ordinary
                    // `x := 42` for a boolean `x` already reports an error.
                    if (!At->isOrdinal())
                        error(S.Args[I]->Loc, diag::err_schema_new_disc_type,
                              {Disc.Name, Pointee->SchemaName});
                    else if (Disc.Ty && !Disc.Ty->isError()
                                 && !isAssignCompatible(*Disc.Ty, *At))
                        error(S.Args[I]->Loc, diag::err_assign_mismatch,
                              {At->Name, Disc.Ty->Name});
                } else if (ToVariant) {
                    if (Vp) {
                        Vp = checkVariantTagArg(Lo, *S.Args[I], *At, *Vp);
                    } else if (!ExtraReported) {
                        error(S.Args[I]->Loc, diag::err_variant_tag_arg_extra,
                              {Lo, Pointee->Name});
                        ExtraReported = true;
                    }
                }
            }
            if (ToSchema && S.Args.size() - 1 != Pointee->SchemaDiscs.size())
                error(S.Loc, diag::err_schema_new_needs_discs,
                      {Pointee->SchemaName,
                       std::to_string(Pointee->SchemaDiscs.size()),
                       std::to_string(S.Args.size() - 1)});
            // §6.6.5.3 gives the extra arguments exactly two readings: variant
            // case-constants for a record with a variant part, or EP §6.7.5.3
            // discriminants for a schema.  A domain that is neither had them
            // evaluated and then dropped, so `new(p, 20)` for a `^string` --
            // where `string` is the unbounded string and not the schema --
            // allocated the default and silently lost the 20.
            if (!ToSchema && !ToVariant && S.Args.size() > 1 && Pointee && !Pointee->isError())
                // EP §6.4.3.3 does make `string` a schema with a capacity
                // discriminant, so `new(p, 20)` for a `^string` is legal there
                // and only plang's modelling of the bare name as the unbounded
                // string makes it not.  Say which it is rather than claiming
                // the standard calls it neither.
                error(S.Args[1]->Loc,
                      Pointee->Kind == TypeKind::String
                          ? diag::err_new_string_capacity
                          : diag::err_new_extra_args,
                      {Pointee->Name});
            return;
        }

        // ISO §6.6.5.3: dispose's extra arguments are new's, read the same
        // way -- case-constants selecting a path through a variant record's
        // nesting, each checked against that level's own tag type.  dispose
        // had no argument checking of any kind before this, schema or
        // variant: arity aside, `dispose(p, 42)` was accepted whatever
        // `p`'s domain type was and whatever 42 was supposed to select.
        // Schema discriminants are deliberately not re-checked here: unlike
        // a variant's case-constants, they are not re-supplied to dispose at
        // all (the instance already carries them from new), so there is
        // nothing here for them to be checked against.
        if (Lo == "dispose" && !S.Args.empty()) {
            auto PtrTy = checkExpr(*S.Args[0]);
            // Mirrors new's own check just above (issue #206): every check
            // below is keyed off Pointee, which only a genuine Pointer arg0
            // computes -- anything else left it null and silently no-opped
            // rather than rejecting the call, so `dispose(i)` for a
            // non-pointer i reached CodeGen with no pointee to size and hit
            // an LLVM IR verifier abort instead of a clean diagnostic.
            if (!PtrTy->isError() && PtrTy->Kind != TypeKind::Pointer)
                error(S.Args[0]->Loc, diag::err_dispose_arg_not_pointer, {PtrTy->Name});
            const Type* Pointee = PtrTy->Kind == TypeKind::Pointer
                                      ? PtrTy->PointeeType.get() : nullptr;
            const bool ToVariant = Pointee && Pointee->Kind == TypeKind::Record
                                    && Pointee->RecordDecl && Pointee->RecordDecl->Variant;
            // Turbo Tier 5, Cluster A item 6: dispose(p, Dtor[(args)]) --
            // checkNewInit's mirror.  Gated the same way: a bare
            // 'dispose(p)' for an object pointee is unaffected.
            if (!ToVariant && Pointee && Pointee->Kind == TypeKind::Object
                    && S.Args.size() > 1) {
                checkDisposeDone(S, *Pointee);
                return;
            }
            const VariantPart* Vp = ToVariant ? Pointee->RecordDecl->Variant.get() : nullptr;
            bool ExtraReported = false;
            for (size_t I = 1; I < S.Args.size(); ++I) {
                auto At = checkExpr(*S.Args[I]);
                if (!ToVariant) continue;
                if (Vp) {
                    Vp = checkVariantTagArg(Lo, *S.Args[I], *At, *Vp);
                } else if (!ExtraReported) {
                    error(S.Args[I]->Loc, diag::err_variant_tag_arg_extra,
                          {Lo, Pointee->Name});
                    ExtraReported = true;
                }
            }
            return;
        }

        // EP §6.7.5.6: bind and unbind reach past the value of their first
        // argument to the variable itself, so what it may be is fixed.
        if (Lo == "bind" || Lo == "unbind") {
            checkBindingCall(Lo, S.Loc, S.Args);
            return;
        }

        // TP-only: Assign(f, name).  f is a file-variable first argument the
        // same way eof/eoln/position/lastposition/empty are (SemaExpr.cpp's
        // identical check) -- without this, FileVarHelpers::fileVarPtr's
        // IdentExpr fast path (its own comment) hands back ANY variable's
        // storage address with no type check of its own, so `Assign(i, 'x')`
        // for a plain integer i reached the runtime with that address
        // reinterpreted as a PascalFile*, corrupting memory with no
        // diagnostic -- the exact bug class issue #417 closed for
        // position/lastposition/empty.  Stays hand-written rather than
        // moving to Builtins.def's AK_File (issue #306's fourth slice):
        // arg1 (name) is a DIFFERENT kind (string-like) from arg0's File,
        // and checkBuiltinArgKinds applies one tag to the whole call
        // uniformly -- see Builtins.def's own comment on Assign's row.
        if (Lo == "assign" && !S.Args.empty()) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError() && FTy->Kind != TypeKind::File)
                error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
            // The second argument is the filename -- any of Turbo's
            // string-shaped types (ShortString, PChar's pointee-Char
            // exemption aside, a plain literal, EP's own string(n) reached
            // under -std=turbo through a type alias) or a single Char, the
            // same "string-like" set length/index already check just above
            // in checkCallExpr (SemaExpr.cpp) for the identical reason.
            if (S.Args.size() > 1) {
                auto NTy = checkExpr(*S.Args[1]);
                const bool StringLike = isVarStringLike(NTy.get())
                    || isCharStringType(*NTy) || isShortStringLike(NTy.get())
                    || NTy->Kind == TypeKind::Char || NTy->Kind == TypeKind::String;
                if (!NTy->isError() && !StringLike)
                    error(S.Args[1]->Loc, diag::err_string_fn_arg_type, {Lo, NTy->Name});
            }
            return;
        }
        // TP-only: Append(f) -- unlike Assign just above, takes ONLY the
        // file (Builtins.def's own MaxArgs = 1), so its File-kind check is
        // genuinely uniform and is generic now (Builtins.def's AK_File row,
        // issue #306's fourth slice) -- split out of the arm it used to
        // share with Assign into its own, same "check once, no extra
        // restriction" shape Erase/Flush's own migrated arms below have.
        if (Lo == "append" && !S.Args.empty()) {
            (void)checkBuiltinArgKinds(Sym->BuiltinKind, Lo, S.Args);
            return;
        }

        // TP-only: Seek(f, n) -- position f at record n (0-relative, in
        // units of RecSize); takes a typed or untyped BINARY file the same
        // way FilePos/FileSize (checkCallExpr, SemaExpr.cpp) do, and refuses
        // a Text argument (err_binary_file_required) for the identical
        // reason.  Stays hand-written rather than moving to AK_File: arg1
        // (n) is a DIFFERENT kind (Integer) from arg0's File, the same
        // multi-kind shape Assign's own arm just above stays hand-written
        // for.
        if (Lo == "seek" && !S.Args.empty()) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError()) {
                if (FTy->Kind != TypeKind::File)
                    error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
                else if (isTextFile(*FTy, Opts))
                    error(S.Args[0]->Loc, diag::err_binary_file_required,
                          {Lo, FTy->Name});
            }
            if (S.Args.size() > 1) {
                auto NTy = checkExpr(*S.Args[1]);
                if (!NTy->isError() && !NTy->isIntegral())
                    error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, NTy->Name});
            }
            return;
        }
        // TP-only: Truncate(f) -- truncate f at the current position; unlike
        // Seek just above, takes ONLY the file, so its File-kind and
        // binary-only checks are the same "base check generic, extra
        // restriction hand-written" shape FilePos/FileSize/SeekEof/SeekEoln
        // already have (SemaExpr.cpp) -- split out of the arm it used to
        // share with Seek into its own.
        if (Lo == "truncate" && !S.Args.empty()) {
            auto Types = checkBuiltinArgKinds(Sym->BuiltinKind, Lo, S.Args);
            auto& FTy = Types[0];
            if (!FTy->isError() && FTy->Kind == TypeKind::File && isTextFile(*FTy, Opts))
                error(S.Args[0]->Loc, diag::err_binary_file_required,
                      {Lo, FTy->Name});
            return;
        }
        // TP-only: BlockRead(f, var buf; count[; var result]) /
        // BlockWrite(f, var buf; count[; var result]) -- see Builtins.def's
        // own comment for the arity-dependent short-transfer behavior. buf
        // is "untyped" the same way FillChar/Move's own X/Source/Dest are,
        // above: any variable at all, its own declared type not otherwise
        // examined.
        if ((Lo == "blockread" || Lo == "blockwrite") && S.Args.size() >= 3) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError()) {
                if (FTy->Kind != TypeKind::File)
                    error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
                else if (isTextFile(*FTy, Opts))
                    error(S.Args[0]->Loc, diag::err_binary_file_required,
                          {Lo, FTy->Name});
            }
            auto BufTy = checkExpr(*S.Args[1]);
            if (!BufTy->isError() && !isLValue(*S.Args[1]))
                error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view("2"), std::string_view(Lo)});
            auto CountTy = checkExpr(*S.Args[2]);
            if (!CountTy->isError() && !CountTy->isIntegral())
                error(S.Args[2]->Loc, diag::err_numeric_argument, {Lo, CountTy->Name});
            if (S.Args.size() > 3) {
                auto ResTy = checkExpr(*S.Args[3]);
                if (!ResTy->isError()) {
                    if (!ResTy->isIntegral())
                        error(S.Args[3]->Loc, diag::err_numeric_argument,
                              {Lo, ResTy->Name});
                    else if (!isLValue(*S.Args[3]))
                        error(S.Args[3]->Loc, diag::err_var_param_needs_lvalue,
                              {std::string_view("4"), std::string_view(Lo)});
                }
            }
            return;
        }
        // TP-only: Erase(f) -- delete the file f is bound to.  Takes any
        // file kind (Text included -- real Turbo Pascal's Erase works on
        // Text files too, unlike Seek/Truncate/BlockRead/BlockWrite/
        // FilePos/FileSize above); the fmClosed requirement (Builtins.def's
        // own comment) is a RUNTIME check (runtime/plang_file.cpp), not a
        // Sema one -- f's Mode is not known statically.  One argument, one
        // kind: generic now (Builtins.def's AK_File row, issue #306's
        // fourth slice).
        if (Lo == "erase" && !S.Args.empty()) {
            (void)checkBuiltinArgKinds(Sym->BuiltinKind, Lo, S.Args);
            return;
        }
        // TP-only: Rename(f, newname) -- same fmClosed requirement as Erase
        // just above.  Stays hand-written rather than AK_File: arg1
        // (newname) is a DIFFERENT kind (string-like) from arg0's File, the
        // same multi-kind shape Assign's own filename argument keeps
        // hand-written for.
        if (Lo == "rename" && S.Args.size() > 1) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError() && FTy->Kind != TypeKind::File)
                error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
            // The new name -- same "string-like" set Assign's own filename
            // argument accepts just above.
            auto NTy = checkExpr(*S.Args[1]);
            const bool StringLike = isVarStringLike(NTy.get())
                || isCharStringType(*NTy) || isShortStringLike(NTy.get())
                || NTy->Kind == TypeKind::Char || NTy->Kind == TypeKind::String;
            if (!NTy->isError() && !StringLike)
                error(S.Args[1]->Loc, diag::err_string_fn_arg_type, {Lo, NTy->Name});
            return;
        }
        // TP-only: Flush(f) -- flush f's buffered output.  No Text/binary
        // restriction (confirmed against `fpc -Mtp`: both accept it).  One
        // argument, one kind: generic now (Builtins.def's AK_File row).
        if (Lo == "flush" && !S.Args.empty()) {
            (void)checkBuiltinArgKinds(Sym->BuiltinKind, Lo, S.Args);
            return;
        }
        // TP-only: SetTextBuf(var f: Text; var buf[; size]).  f must be
        // Text (err_line_proc_not_text, the same restriction Eoln/SeekEoln
        // already have); buf is untyped like BlockRead/BlockWrite's own
        // above.
        if (Lo == "settextbuf" && S.Args.size() >= 2) {
            auto FTy = checkExpr(*S.Args[0]);
            if (!FTy->isError()) {
                if (FTy->Kind != TypeKind::File)
                    error(S.Args[0]->Loc, diag::err_file_argument, {Lo, FTy->Name});
                else if (!isTextFile(*FTy, Opts))
                    error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                          {Lo, FTy->Name});
            }
            auto BufTy = checkExpr(*S.Args[1]);
            if (!BufTy->isError() && !isLValue(*S.Args[1]))
                error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view("2"), std::string_view(Lo)});
            if (S.Args.size() > 2) {
                auto SizeTy = checkExpr(*S.Args[2]);
                if (!SizeTy->isError() && !SizeTy->isIntegral())
                    error(S.Args[2]->Loc, diag::err_numeric_argument,
                          {Lo, SizeTy->Name});
            }
            return;
        }

        // TP-only: Assert(cond: Boolean [; msg: string]).  Type-checked
        // exactly the same whether or not {$C-} is in effect at S.Loc --
        // CGProcCall::emitCallStmt is where the switch is actually read, and
        // it decides only whether the call is generated at all, not whether
        // it type-checks; a program with assertions off still gets told
        // about a type error in a call it wrote.  The message argument reuses
        // err_string_fn_arg_type, the same "must be char or string" check
        // EQ/LT/GT/... already give their own arguments, rather than a
        // second, near-identical diagnostic just for this.
        if (Lo == "assert" && !S.Args.empty()) {
            auto CondTy = checkExpr(*S.Args[0]);
            if (!CondTy->isError() && CondTy->Kind != TypeKind::Boolean)
                error(S.Args[0]->Loc, diag::err_assert_cond_not_boolean,
                      {CondTy->Name});
            if (S.Args.size() > 1) {
                auto MsgTy = checkExpr(*S.Args[1]);
                if (!MsgTy->isError() && !isCharStringType(*MsgTy)
                        && MsgTy->Kind != TypeKind::String
                        && MsgTy->Kind != TypeKind::VarString
                        && MsgTy->Kind != TypeKind::Char)
                    error(S.Args[1]->Loc, diag::err_string_fn_arg_type,
                          {Lo, MsgTy->Name});
            }
            return;
        }

        // TP-only: Break/Continue leave/skip the innermost while/for/for-in/
        // repeat loop.  Arity (0 arguments) was already checked against
        // Builtins.def above; this is only the structural-context check
        // Builtins.def cannot express, LoopDepth_ (Sema.h) tracking that
        // context the same way StructStack tracks goto's.
        if (Lo == "break" || Lo == "continue") {
            if (LoopDepth_ == 0)
                error(S.Loc, Lo == "break" ? diag::err_break_outside_loop
                                            : diag::err_continue_outside_loop);
            return;
        }

        // TP-only: Exit([value]).  A value is legal only inside a function
        // -- CurrentRetType is null both at a program's own top level and
        // inside a plain procedure (checkProcBody resets it there), exactly
        // the "Procedures cannot return a value" restriction `fpc -Mtp`
        // itself enforces (confirmed empirically).  The value obeys the same
        // rule an ordinary `FuncName := value` assignment to the function's
        // own result would (err_assign_mismatch, reused rather than a
        // second near-identical diagnostic -- see checkAssign's own use of
        // it) and marks the result assigned the same way checkAssign does
        // (resultFrameFor): a function whose only assignment is
        // `Exit(value)` on every path would otherwise be wrongly refused
        // err_function_no_result, since Exit is a CallStmt and never goes
        // through checkAssign at all.
        if (Lo == "exit") {
            if (!S.Args.empty()) {
                auto ValTy = checkExpr(*S.Args[0]);
                if (!CurrentRetType) {
                    error(S.Loc, diag::err_exit_value_outside_function);
                } else {
                    if (!ValTy->isError() && !CurrentRetType->isError()
                            && !isAssignCompatible(*CurrentRetType, *ValTy))
                        error(S.Args[0]->Loc, diag::err_assign_mismatch,
                              {ValTy->Name, CurrentRetType->Name});
                    checkStringCapacity(*CurrentRetType, *S.Args[0]);
                    warnIfConstantOutOfRange(*CurrentRetType, *S.Args[0]);
                    if (!FuncStack.empty()) FuncStack.back().HasResult = true;
                }
            }
            return;
        }

        // Turbo Tier 5, Cluster A item 6: 'Fail' -- legal only inside a
        // constructor's own body (confirmed against a local fpc -Mtp
        // build: 'Fail' is not even a recognized identifier anywhere
        // else, "Identifier not found"; a direct call to a constructor
        // outside 'new(p, Ctor(...))' -- e.g. 'A.Init(false);' for an
        // already-declared A -- still runs Fail's early-return, it is
        // only the CALLER that has no way to observe it, which fpc -Mtp
        // also confirms is exactly what real Borland/FPC does).
        // CurrentProc->OwnerType.empty() mirrors checkInheritedCallStmt's
        // own "not inside a method at all" test; !CurrentProc->
        // IsConstructor narrows it from "inside any method" to "inside a
        // constructor specifically".
        if (Lo == "fail") {
            if (!CurrentProc || CurrentProc->OwnerType.empty()
                    || !CurrentProc->IsConstructor)
                error(S.Loc, diag::err_fail_outside_constructor, {});
            return;
        }

        // EP §6.7.5.5: readstr(e, v1,...,vn) reads e and assigns every v;
        // writestr(e, ...) is the mirror, assigning only e.  Neither had a
        // dedicated arm before this -- both fell through to the generic
        // "evaluate for side effects only" fallback below, so a non-variable
        // target reached codegen with no address to assign into and hit the
        // same class of abort #224 fixed for read/readln's targets (a
        // codegenICE, or, for writestr, "destination is not a string
        // variable").  Mirrors read/readln's isLValue check above; the
        // read/write split is the same one checkForBody's isWriteArg lambda
        // already uses for the for-loop threat scan (issue #265).
        if ((Lo == "readstr" || Lo == "writestr") && !S.Args.empty()) {
            std::vector<std::shared_ptr<Type>> Ts;
            Ts.reserve(S.Args.size());
            for (const auto& Arg : S.Args) Ts.push_back(checkExpr(*Arg));

            const bool IsReadstr = Lo == "readstr";
            for (size_t I = 0; I < Ts.size(); ++I) {
                // readstr's targets are v1..vn (everything past the source
                // string at index 0); writestr's destination is its lone
                // arg 0, the rest being ordinary write-parameters.
                const bool IsTarget = IsReadstr ? I >= 1 : I == 0;
                if (!IsTarget) continue;
                const auto& T = *Ts[I];
                if (!T.isError() && !isLValue(*S.Args[I]))
                    error(S.Args[I]->Loc,
                          IsReadstr ? diag::err_readstr_not_variable
                                    : diag::err_writestr_not_variable,
                          {T.Name});
            }
            return;
        }

        // TP-only: Include(s, x) / Exclude(s, x) -- `s := s + [x]` and
        // `s := s - [x]` by another name, mutating the set variable s in
        // place (SetOps::emitSetSingleton/emitSetBinary, CodeGen).  x must
        // be assignment-compatible with s's own base type -- the identical
        // requirement checkSetLit's typed-constructor arm already asks of
        // each member of a literal `[x]`.
        if ((Lo == "include" || Lo == "exclude") && S.Args.size() == 2) {
            auto SetTy  = checkExpr(*S.Args[0]);
            auto ElemTy = checkExpr(*S.Args[1]);
            if (!SetTy->isError()) {
                if (SetTy->Kind != TypeKind::Set) {
                    error(S.Args[0]->Loc, diag::err_set_argument, {Lo, SetTy->Name});
                } else if (!isLValue(*S.Args[0])) {
                    error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("1"), std::string_view(Lo)});
                } else if (!ElemTy->isError() && SetTy->ElemType
                           && !SetTy->ElemType->isError()
                           && !isAssignCompatible(*SetTy->ElemType, *ElemTy)) {
                    error(S.Args[1]->Loc, diag::err_assign_mismatch,
                          {ElemTy->Name, SetTy->ElemType->Name});
                }
                // Issue #710: Include/Exclude mutate their first argument in
                // place (SetOps::emitSetSingleton/emitSetBinary writes
                // straight into its storage), the same untyped-target shape
                // FillChar/Move's own X/Dest have below -- a `const`
                // parameter's set is passed by reference (isStructuredForConstByRef),
                // so this reached the caller's own storage with no diagnostic
                // at all before this check existed.
                checkNotProtected(*S.Args[0], S.Args[0]->Loc);
            }
            return;
        }

        // TP-only: Inc(x[, n]) / Dec(x[, n]) mutate x in place by n
        // (defaulting to 1) -- EP §6.7.6.5's succ/pred two-argument form by
        // another name, except succ/pred READ a new value and Inc/Dec WRITE
        // one back into x.  x is either an ordinary ordinal variable, or,
        // under -std=turbo, a PChar-like typed pointer (isCharPointerType,
        // Type.h): Inc/Dec on a pointer go no further than `p + n`/`p - n`
        // already do (checkBinary, above in this file's SemaExpr.cpp
        // sibling), so a pointer Inc/Dec refuses is refused the identical
        // way there. Extending pointer Inc/Dec to every typed pointer, not
        // just PChar-like ones, would need pointer arithmetic itself to
        // support that first -- a separate, concurrent, not-yet-landed
        // change; see CGProcCall's own comment for where this is lowered.
        if ((Lo == "inc" || Lo == "dec") && !S.Args.empty()) {
            auto Ty = checkExpr(*S.Args[0]);
            std::shared_ptr<Type> StepTy;
            if (S.Args.size() > 1) StepTy = checkExpr(*S.Args[1]);
            if (!Ty->isError()) {
                const bool IsPtr = Ty->Kind == TypeKind::Pointer && isCharPointerType(*Ty);
                if (!Ty->isOrdinal() && !IsPtr) {
                    error(S.Args[0]->Loc, diag::err_inc_dec_argument, {Lo, Ty->Name});
                } else if (!isLValue(*S.Args[0])) {
                    error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("1"), std::string_view(Lo)});
                }
                // Issue #710: Inc/Dec write x back in place.  A scalar const
                // parameter is passed BY VALUE, so this only ever mutates the
                // callee's own copy -- but the const contract still promises
                // the parameter is read-only inside the body, and `fpc -Mtp`
                // refuses it too, so the diagnostic belongs here regardless
                // of whether the underlying storage happens to be shared
                // with the caller's.
                checkNotProtected(*S.Args[0], S.Args[0]->Loc);
            }
            // Inc/Dec are TP-only to begin with (Builtins.def's own
            // Dialects mask, already checked above via checkEPOnly), so --
            // unlike succ/pred's EP-vs-ISO7185 split -- the two-argument
            // form needs no dialect gate of its own here.
            if (StepTy && !StepTy->isError() && !StepTy->isIntegral())
                error(S.Args[1]->Loc, diag::err_step_argument_not_integer,
                      {Lo, StepTy->Name});
            return;
        }

        // TP-only: FillChar(var X; Count: Integer; Value) -- X is "untyped"
        // the way real Turbo Pascal's is: any variable at all, its own
        // declared type not otherwise examined, addressed directly and
        // filled byte-for-byte (CGProcCall lowers this to llvm.memset).
        if (Lo == "fillchar" && S.Args.size() == 3) {
            auto XTy    = checkExpr(*S.Args[0]);
            auto CountTy = checkExpr(*S.Args[1]);
            auto ValTy   = checkExpr(*S.Args[2]);
            if (!XTy->isError() && !isLValue(*S.Args[0]))
                error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view("1"), std::string_view(Lo)});
            // Issue #710: X is written directly (CGProcCall lowers this to
            // llvm.memset over X's own address) -- a structured `const`
            // parameter is passed by reference, so this reached and
            // overwrote the CALLER's storage with no diagnostic at all
            // before this check existed.
            if (!XTy->isError()) checkNotProtected(*S.Args[0], S.Args[0]->Loc);
            if (!CountTy->isError() && !CountTy->isIntegral())
                error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, CountTy->Name});
            // Value is a byte or a char in real Turbo Pascal -- both
            // ordinal, and only the low 8 bits of either are ever used.
            if (!ValTy->isError() && !ValTy->isOrdinal())
                error(S.Args[2]->Loc, diag::err_ordinal_argument, {Lo, ValTy->Name});
            return;
        }
        // TP-only: Move(const Source; var Dest; Count: Integer).  Source and
        // Dest are "untyped" the same way FillChar's X is above; Count is
        // again a BYTE count (FPC field practice), not an element count.
        // Lowered to llvm.memmove specifically, not memcpy -- Source and
        // Dest may legally overlap and FPC's own Move handles that
        // correctly, so plang's has to as well; see CGProcCall's own
        // comment on the argument order, which is the classic place a Move
        // port gets this backwards.
        if (Lo == "move" && S.Args.size() == 3) {
            auto SrcTy   = checkExpr(*S.Args[0]);
            auto DstTy   = checkExpr(*S.Args[1]);
            auto CountTy = checkExpr(*S.Args[2]);
            if (!SrcTy->isError() && !isLValue(*S.Args[0]))
                error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view("1"), std::string_view(Lo)});
            // Source is only ever READ (llvm.memmove's source operand), so
            // unlike Dest just below it needs no protected-ness check --
            // reading through a const/protected parameter is exactly what
            // it is for.
            if (!DstTy->isError() && !isLValue(*S.Args[1]))
                error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                      {std::string_view("2"), std::string_view(Lo)});
            // Issue #710: Dest is written directly (llvm.memmove's
            // destination operand) -- a structured `const` parameter is
            // passed by reference, so this reached and overwrote the
            // CALLER's storage with no diagnostic at all before this check
            // existed.
            if (!DstTy->isError()) checkNotProtected(*S.Args[1], S.Args[1]->Loc);
            if (!CountTy->isError() && !CountTy->isIntegral())
                error(S.Args[2]->Loc, diag::err_numeric_argument, {Lo, CountTy->Name});
            return;
        }

        // TP-only: the System-unit ShortString routines that mutate a var
        // parameter -- Delete/Insert/SetLength/Str/Val.  isTurboStringLike
        // mirrors checkCallExpr's identical local lambda (SemaExpr.cpp) for
        // Copy/Pos/Concat/StringOfChar: ShortString, Char or a plain
        // literal/String, deliberately NOT the ISO/EP char-array shape
        // (isCharStringType) Turbo's own operators never widen for either.
        auto isTurboStringLike = [](const std::shared_ptr<Type>& T) {
            return isShortStringLike(T.get()) || T->Kind == TypeKind::Char
                || T->Kind == TypeKind::String;
        };
        // Delete(var s: string; index, count: Integer) -- mutates s in
        // place; Builtins.def's own comment on the out-of-range-index
        // no-op rule (which, unlike Copy, is NOT a clamp).
        if (Lo == "delete" && S.Args.size() == 3) {
            auto ST = checkExpr(*S.Args[0]);
            auto IT = checkExpr(*S.Args[1]);
            auto CT = checkExpr(*S.Args[2]);
            if (!ST->isError()) {
                if (!isShortStringLike(ST.get()))
                    error(S.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
                else if (!isLValue(*S.Args[0]))
                    error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("1"), std::string_view(Lo)});
                // Issue #710: s is mutated in place -- same untyped-target
                // shape FillChar/Move have above, and the same missing
                // check.
                checkNotProtected(*S.Args[0], S.Args[0]->Loc);
            }
            if (!IT->isError() && !IT->isIntegral())
                error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, IT->Name});
            if (!CT->isError() && !CT->isIntegral())
                error(S.Args[2]->Loc, diag::err_numeric_argument, {Lo, CT->Name});
            return;
        }
        // Insert(source: string; var s: string; index: Integer) -- mutates s
        // in place; source is a plain (value) argument, s is the var one.
        if (Lo == "insert" && S.Args.size() == 3) {
            auto SrcT = checkExpr(*S.Args[0]);
            auto ST   = checkExpr(*S.Args[1]);
            auto IT   = checkExpr(*S.Args[2]);
            if (!SrcT->isError() && !isTurboStringLike(SrcT))
                error(S.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, SrcT->Name});
            if (!ST->isError()) {
                if (!isShortStringLike(ST.get()))
                    error(S.Args[1]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
                else if (!isLValue(*S.Args[1]))
                    error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("2"), std::string_view(Lo)});
                // Issue #710: s is mutated in place; source (arg 0) is only
                // ever read, so it needs no check of its own -- Move's own
                // Source/Dest split above is the same asymmetry.
                checkNotProtected(*S.Args[1], S.Args[1]->Loc);
            }
            if (!IT->isError() && !IT->isIntegral())
                error(S.Args[2]->Loc, diag::err_numeric_argument, {Lo, IT->Name});
            return;
        }
        // SetLength(var s: string; newLength: Integer) -- sets s's own
        // length byte, clamped to s's declared capacity.
        if (Lo == "setlength" && S.Args.size() == 2) {
            auto ST = checkExpr(*S.Args[0]);
            auto LT = checkExpr(*S.Args[1]);
            if (!ST->isError()) {
                if (!isShortStringLike(ST.get()))
                    error(S.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
                else if (!isLValue(*S.Args[0]))
                    error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("1"), std::string_view(Lo)});
                // Issue #710: s's length byte is mutated in place.
                checkNotProtected(*S.Args[0], S.Args[0]->Loc);
            }
            if (!LT->isError() && !LT->isIntegral())
                error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, LT->Name});
            return;
        }
        // Str(x [: width [: decimals]]; var s: string) -- formats x the same
        // way write(x [: width [: decimals]]) does (reusing that machinery,
        // BuiltinIO.cpp), into ShortString destination s.  x's writable-type
        // set mirrors the write/writeln check above exactly (ParseStmt.cpp's
        // parser already attaches width/decimals to a WriteParam the same
        // way, so the value to check is Wp->Value, not the WriteParam itself).
        if (Lo == "str" && S.Args.size() == 2) {
            const ExprNode* XArg = S.Args[0].get();
            if (auto* Wp = llvm::dyn_cast<WriteParam>(XArg)) XArg = Wp->Value.get();
            auto XT = checkExpr(*S.Args[0]);
            auto ST = checkExpr(*S.Args[1]);
            if (!XT->isError()) {
                switch (XT->Kind) {
                case TypeKind::Integer: case TypeKind::Real:
                case TypeKind::Boolean: case TypeKind::Char:
                case TypeKind::Subrange: case TypeKind::Enum:
                    break;
                default:
                    error(XArg->Loc, diag::err_write_param_type, {XT->Name});
                }
            }
            if (!ST->isError()) {
                if (!isShortStringLike(ST.get()))
                    error(S.Args[1]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
                else if (!isLValue(*S.Args[1]))
                    error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("2"), std::string_view(Lo)});
                // Issue #710: s is the formatted-output destination, written
                // in place.
                checkNotProtected(*S.Args[1], S.Args[1]->Loc);
            }
            return;
        }
        // Val(s: string; var v; var code: Integer) -- parses s into v (an
        // Integer- or Real-kind variable, any width; NOT Char/Boolean/Enum --
        // confirmed against `fpc -Mtp`: "Integer or real expression
        // expected" for any other destination type) and sets code to 0 on
        // success or the 1-based index of the first bad character on
        // failure.  s is a plain value argument (may be a literal); v and
        // code are both var parameters.
        if (Lo == "val" && S.Args.size() == 3) {
            auto ST = checkExpr(*S.Args[0]);
            auto VT = checkExpr(*S.Args[1]);
            auto CT = checkExpr(*S.Args[2]);
            if (!ST->isError() && !isTurboStringLike(ST))
                error(S.Args[0]->Loc, diag::err_string_fn_arg_type, {Lo, ST->Name});
            if (!VT->isError()) {
                if (!VT->isIntegral() && VT->Kind != TypeKind::Real)
                    error(S.Args[1]->Loc, diag::err_val_argument, {VT->Name});
                else if (!isLValue(*S.Args[1]))
                    error(S.Args[1]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("2"), std::string_view(Lo)});
            }
            if (!CT->isError()) {
                if (!CT->isIntegral())
                    error(S.Args[2]->Loc, diag::err_numeric_argument, {Lo, CT->Name});
                else if (!isLValue(*S.Args[2]))
                    error(S.Args[2]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("3"), std::string_view(Lo)});
            }
            return;
        }

        // GetMem(var P: Pointer; Size: Int64) -- P is a var/out-parameter
        // (runtime/plang_sys.cpp's plang_tp_getmem writes the new address
        // back through it), so unlike FreeMem's own P just below, it needs
        // isLValue too, exactly like new's own first-argument check just
        // above.  Size is Int64, not real Borland TP7's 16-bit Word --
        // Builtins.def's own comment on why.
        if (Lo == "getmem" && S.Args.size() == 2) {
            auto PTy    = checkExpr(*S.Args[0]);
            auto SizeTy = checkExpr(*S.Args[1]);
            if (!PTy->isError()) {
                if (PTy->Kind != TypeKind::Pointer)
                    error(S.Args[0]->Loc, diag::err_heap_arg_not_pointer,
                          {Lo, PTy->Name});
                else if (!isLValue(*S.Args[0]))
                    error(S.Args[0]->Loc, diag::err_var_param_needs_lvalue,
                          {std::string_view("1"), std::string_view(Lo)});
            }
            if (!SizeTy->isError() && !SizeTy->isIntegral())
                error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, SizeTy->Name});
            return;
        }
        // FreeMem(P: Pointer[, Size: Int64]) -- P is a plain value argument,
        // not `var`: confirmed against the local `fpc -Mtp` install's own
        // rtl/inc/heap.inc, whose FreeMem takes `p:pointer` with no `var`,
        // unlike GetMem's own out-parameter P just above.  Size, when given,
        // is accepted but never checked against what the matching GetMem was
        // actually given (runtime/plang_sys.cpp's plang_tp_freemem's own
        // comment) -- matching real Borland/FPC field practice.
        if (Lo == "freemem" && !S.Args.empty()) {
            auto PTy = checkExpr(*S.Args[0]);
            if (!PTy->isError() && PTy->Kind != TypeKind::Pointer)
                error(S.Args[0]->Loc, diag::err_heap_arg_not_pointer, {Lo, PTy->Name});
            if (S.Args.size() > 1) {
                auto SizeTy = checkExpr(*S.Args[1]);
                if (!SizeTy->isError() && !SizeTy->isIntegral())
                    error(S.Args[1]->Loc, diag::err_numeric_argument, {Lo, SizeTy->Name});
            }
            return;
        }

        // Generic: arity was already checked against Builtins.def above.
        // Every builtin with its own ArgKind (Builtins.def's own column --
        // Ordinal for ord/chr/odd, Numeric for sqrt/abs/sqr/..., ...) falls
        // through every dedicated arm above to land here when called as a
        // bare statement, the same way checkCallExpr's OWN generic fallback
        // (SemaExpr.cpp, at the very end of its Func arm) is where an
        // expression-context call of one of these lands once none of ITS
        // dedicated arms matches either -- except that one calls
        // checkBuiltinArgKinds (via each dedicated `if (Lo == ...)` arm
        // above it) for every ArgKind-migrated builtin, and this one, until
        // now, never did, for ANY builtin, in either its own dedicated arms
        // or this fallback.  A required FUNCTION reaches here at all only
        // via Turbo's `{$X+}` (checked above -- Sym->IsFunction gate), so
        // `ord(1.5);` written as a bare statement under `{$X+}` skipped
        // checkBuiltinArgKinds entirely and reached CodeGen's unconditional
        // zext, aborting the compiler with an LLVM IR verifier failure
        // instead of a diagnostic (issue #547) -- the exact ICE class issue
        // #212 fixed for the expression-context path, left open here.
        // checkBuiltinArgKinds is a documented no-op beyond calling checkExpr
        // once per argument (Sema.h's own comment: "emits nothing... for
        // AK_Any, which is every builtin this column has not migrated yet"),
        // so swapping it in here is behavior-preserving for the AK_Any
        // majority (write, read, reset, ... -- though most of those already
        // returned via their own dedicated arm above and never reach this
        // fallback at all) and adds the missing check for every ArgKind
        // builtin, migrated now or in the future, with no risk of
        // checkExpr running twice on the same argument (issue #272) since
        // this is the sole remaining evaluation of S.Args on this path.
        (void)checkBuiltinArgKinds(Sym->BuiltinKind, Lo, S.Args);
        return;
    }
    // Turbo procedural VALUES: see the identical mark in checkCallExpr
    // (SemaExpr.cpp) -- Phase 7's unused-variable audit only looks at
    // SymbolKind::Var, so calling f() as a statement is a use of f the same
    // way calling it in an expression is.
    if (Sym->Kind == SymbolKind::Var) Sym->Referenced = true;
    // Issue #730: see checkCallExpr's identical gate (SemaExpr.cpp) for the
    // full story -- a self-scope FIELD symbol (pushMethodSelfScope) can
    // share a name with a sibling METHOD, and the plain lookup above finds
    // the (non-callable) field first.  Give the implicit-method fallback a
    // chance to find that sibling method before falling through to
    // checkUserDefinedCall's own err_not_callable; checkImplicitMethodCallStmt
    // returns false, leaving S untouched, when no such method exists, so a
    // field with no same-named method sibling still reaches the ordinary
    // diagnosis below, unchanged.
    if (Sym->IsSelfScopeField && !(Sym->Ty && isCallable(*Sym->Ty))
            && checkImplicitMethodCallStmt(S))
        return;
    // Turbo `{$X+}`: when checkUserDefinedCall lets a function through this
    // way, the type it returns is the callee's real result type -- record it
    // on the CallStmt so codegen's own external-declaration fallback
    // (CGProcCall::emitUserProcCall) knows it too, instead of guessing void.
    // TyErr (an ordinary procedure call, or any error) collapses back to
    // null: see CallStmt::ResolvedType's own comment.
    auto RetTy = checkUserDefinedCall(*Sym, S.Loc, S.Args, /*expectFunction=*/false);
    S.ResolvedType = (RetTy && !RetTy->isError()) ? RetTy : nullptr;
}

// Turbo Tier 5, issues #571/#623: see this function's own declaration
// (Sema.h) for when checkCallStmt tries this.
bool Sema::checkImplicitMethodCallStmt(const CallStmt& S) {
    const ImplicitMethodLookup IL = findImplicitCallMethod(S.Name);
    if (!IL.M) return false;

    // Turbo Tier 5, Cluster A item 7: same private-visibility gate
    // checkMethodCall applies -- see its own comment (SemaExpr.cpp).
    if (IL.M->IsPrivate && IL.Owner->DeclaringModule != CurrentUnit_)
        error(S.Loc, diag::err_object_private_method, {IL.Owner->Name, S.Name});

    // Hand off to the SAME arity/argument-type checking an ordinary
    // procedure/function call gets -- the identical synthetic-Proc-stand-in
    // trick checkMethodCall's own tail uses.
    Symbol Indirect;
    Indirect.Kind       = SymbolKind::Proc;
    Indirect.Name       = IL.Owner->Name + "." +
                           (IL.M->Heading ? IL.M->Heading->Name : S.Name);
    Indirect.IsFunction = IL.M->IsFunction;
    Indirect.Params     = IL.M->Params;
    Indirect.ReturnType = IL.M->RetType;
    // Turbo `{$X+}`: mirrors checkCallStmt's own identical CallStmt::
    // ResolvedType annotation just above, for a function method let through
    // as a statement with its result discarded.
    auto RetTy = checkUserDefinedCall(Indirect, S.Loc, S.Args, /*ExpectFunction=*/false);
    S.ResolvedType = (RetTy && !RetTy->isError()) ? RetTy : nullptr;
    S.ImplicitMethodReceiverType = IL.Receiver;
    return true;
}

// Turbo Tier 5, Cluster A item 3: 'Obj.Method(args);' / 'P^.Method(args);' /
// the bare-call form 'Obj.Method;' used as a statement -- see
// Sema::checkMethodCall's own comment (SemaExpr.cpp) for the shared logic
// with checkMethodCallExpr, and MethodCallStmt::ResolvedType's own comment
// (AstStmt.h) for the Turbo `{$X+}` result-discard mirror of CallStmt's
// identical field just above.
void Sema::checkMethodCallStmt(const MethodCallStmt& S) {
    auto RetTy = checkMethodCall(*S.Receiver, S.Method, S.Loc, S.Args,
                                  /*ExpectFunction=*/false);
    S.ResolvedType = (RetTy && !RetTy->isError()) ? RetTy : nullptr;
}

// Turbo procedural VALUES (issue #648): 'a[i](args);' / 'p^(args);' used as
// a statement -- see Sema::checkIndirectCall's own comment (SemaExpr.cpp)
// for the shared logic with checkIndirectCallExpr, and
// IndirectCallStmt::ResolvedType's own comment (AstStmt.h) for the Turbo
// `{$X+}` result-discard mirror of CallStmt/MethodCallStmt's identical field.
void Sema::checkIndirectCallStmt(const IndirectCallStmt& S) {
    auto RetTy = checkIndirectCall(*S.Callee, S.Loc, S.Args, /*ExpectFunction=*/false);
    S.ResolvedType = (RetTy && !RetTy->isError()) ? RetTy : nullptr;
}

// Turbo Tier 5, Cluster A item 5: see InheritedCallStmt's own comment
// (AstStmt.h) for the whole design.  Confirmed against a local `fpc -Mtp`
// build: 'inherited' always resolves STATICALLY, to whichever ancestor's own
// body the name-lookup below finds -- never through the VMT, even when that
// body is itself declared 'virtual' (an override calling 'inherited' from
// inside a call that itself arrived via virtual dispatch reaches the direct
// parent's own body exactly once, never redispatching and never looping).
//
// Issue #509: the ancestor-resolution logic itself now lives in
// checkInheritedCall (SemaExpr.cpp), shared with checkInheritedCallExpr --
// this wrapper is unchanged in OBSERVABLE behavior from before that
// extraction: same diagnostics, same S.ResolvedMethod/ImplementingType/
// ImplementingModule/ResolvedType results for every input that was already
// legal or already refused.
void Sema::checkInheritedCallStmt(const InheritedCallStmt& S) {
    auto RetTy = checkInheritedCall(S.Method, S.Loc, S.Args, /*ExpectFunction=*/false,
                                    S.ResolvedMethod, S.ImplementingType, S.ImplementingModule,
                                    S.ImplementingOwnerType);
    S.ResolvedType = (RetTy && !RetTy->isError()) ? RetTy : nullptr;
}

namespace {
/// Turbo Tier 5, Cluster A item 6: the second argument of 'new(p, X)' or
/// 'dispose(p, X)' for an object pointee is written one of two ways real
/// Borland/FPC both accept (confirmed against a local fpc -Mtp build) --
/// a bare identifier for a no-argument constructor/destructor ('Init',
/// 'Done'), or a CallExpr when there are arguments ('Init(a, b)').  Pulls
/// the callee name and argument list out of whichever shape it is;
/// returns false (name left untouched) for anything else, e.g. a literal
/// or an arbitrary expression, which is not a legal callee shape either
/// way.
bool splitCtorDtorArg(const ExprNode& E, std::string& Name,
                      std::span<const std::unique_ptr<ExprNode>>& Args) {
    if (auto* CE = llvm::dyn_cast<CallExpr>(&E)) {
        Name = CE->Name;
        Args = CE->Args;
        return true;
    }
    if (auto* Id = llvm::dyn_cast<IdentExpr>(&E)) {
        Name = Id->Name;
        Args = {};
        return true;
    }
    return false;
}
} // namespace

void Sema::checkNewInit(const CallStmt& S, const Type& Pointee) {
    if (S.Args.size() != 2) {
        error(S.Loc, diag::err_new_object_needs_init, {Pointee.Name});
        for (size_t I = 1; I < S.Args.size(); ++I) (void)checkExpr(*S.Args[I]);
        return;
    }
    std::string CtorName;
    std::span<const std::unique_ptr<ExprNode>> CtorArgs;
    if (!splitCtorDtorArg(*S.Args[1], CtorName, CtorArgs)) {
        error(S.Args[1]->Loc, diag::err_new_object_needs_init, {Pointee.Name});
        (void)checkExpr(*S.Args[1]);
        return;
    }

    // Same ancestor-chain walk checkMethodCall/checkInheritedCallStmt both
    // use (Sema::findObjectMethod, issue #621) -- a constructor need not be
    // declared directly on Pointee itself (an unmodified inherited one is
    // legal: 'new(pDog, Init)' where TDog declares no Init of its own but
    // TAnimal does).
    const auto ML = findObjectMethod(Pointee, CtorName);
    if (!ML.M) {
        error(S.Args[1]->Loc, diag::err_object_method_not_found,
              {Pointee.Name, CtorName});
        for (const auto& A : CtorArgs) (void)checkExpr(*A);
        return;
    }
    if (!ML.M->IsConstructor) {
        error(S.Args[1]->Loc, diag::err_new_init_not_constructor,
              {Pointee.Name, CtorName});
        for (const auto& A : CtorArgs) (void)checkExpr(*A);
        return;
    }
    // Turbo Tier 5, Cluster A item 7: same private-visibility gate
    // checkMethodCall applies -- see its own comment (SemaExpr.cpp).
    if (ML.M->IsPrivate && ML.Owner->DeclaringModule != CurrentUnit_)
        error(S.Args[1]->Loc, diag::err_object_private_method,
              {ML.Owner->Name, CtorName});

    Symbol Indirect;
    Indirect.Kind       = SymbolKind::Proc;
    Indirect.Name       = ML.Owner->Name + "." +
                           (ML.M->Heading ? ML.M->Heading->Name : CtorName);
    Indirect.IsFunction = ML.M->IsFunction;
    Indirect.Params     = ML.M->Params;
    Indirect.ReturnType = ML.M->RetType;
    (void)checkUserDefinedCall(Indirect, S.Args[1]->Loc, CtorArgs,
                               /*ExpectFunction=*/false);
    S.NewInitMethod = CtorName;
}

// Issue #622: see this method's own declaration (Sema.h) for the whole
// design -- checkNewInit's sibling for New's FUNCTION form.
void Sema::checkNewInitExpr(const CallExpr& E, const Type& Pointee) {
    if (E.Args.size() != 2) {
        error(E.Loc, diag::err_new_function_object_needs_init, {Pointee.Name});
        for (size_t I = 1; I < E.Args.size(); ++I) (void)checkExpr(*E.Args[I]);
        return;
    }
    std::string CtorName;
    std::span<const std::unique_ptr<ExprNode>> CtorArgs;
    if (!splitCtorDtorArg(*E.Args[1], CtorName, CtorArgs)) {
        error(E.Args[1]->Loc, diag::err_new_function_object_needs_init, {Pointee.Name});
        (void)checkExpr(*E.Args[1]);
        return;
    }

    const Symbol* MethodSym = nullptr;
    for (const Type* Cur = &Pointee; Cur; Cur = Cur->Parent.get()) {
        Symbol* Sy = Symtab.lookup(objectMethodKey(Cur->Name, CtorName));
        if (Sy && Sy->Kind == SymbolKind::Method) { MethodSym = Sy; break; }
    }
    if (!MethodSym) {
        error(E.Args[1]->Loc, diag::err_object_method_not_found,
              {Pointee.Name, CtorName});
        for (const auto& A : CtorArgs) (void)checkExpr(*A);
        return;
    }
    if (!MethodSym->IsMethodConstructor) {
        error(E.Args[1]->Loc, diag::err_new_init_not_constructor,
              {Pointee.Name, CtorName});
        for (const auto& A : CtorArgs) (void)checkExpr(*A);
        return;
    }
    if (MethodSym->IsMethodPrivate && MethodSym->Module != CurrentUnit_)
        error(E.Args[1]->Loc, diag::err_object_private_method,
              {MethodSym->MethodOwnerType, CtorName});

    Symbol Indirect;
    Indirect.Kind       = SymbolKind::Proc;
    Indirect.Name       = MethodSym->MethodOwnerType + "." +
                           (MethodSym->Decl ? MethodSym->Decl->Name : CtorName);
    Indirect.IsFunction = MethodSym->IsFunction;
    Indirect.Params     = MethodSym->Params;
    Indirect.ReturnType = MethodSym->ReturnType;
    (void)checkUserDefinedCall(Indirect, E.Args[1]->Loc, CtorArgs,
                               /*ExpectFunction=*/false);
}

void Sema::checkDisposeDone(const CallStmt& S, const Type& Pointee) {
    if (S.Args.size() != 2) {
        error(S.Loc, diag::err_dispose_object_needs_done, {Pointee.Name});
        for (size_t I = 1; I < S.Args.size(); ++I) (void)checkExpr(*S.Args[I]);
        return;
    }
    std::string DtorName;
    std::span<const std::unique_ptr<ExprNode>> DtorArgs;
    if (!splitCtorDtorArg(*S.Args[1], DtorName, DtorArgs)) {
        error(S.Args[1]->Loc, diag::err_dispose_object_needs_done, {Pointee.Name});
        (void)checkExpr(*S.Args[1]);
        return;
    }

    // Same ancestor-chain walk checkNewInit uses just above (Sema::
    // findObjectMethod, issue #621).
    const auto ML = findObjectMethod(Pointee, DtorName);
    if (!ML.M) {
        error(S.Args[1]->Loc, diag::err_object_method_not_found,
              {Pointee.Name, DtorName});
        for (const auto& A : DtorArgs) (void)checkExpr(*A);
        return;
    }
    if (!ML.M->IsDestructor) {
        error(S.Args[1]->Loc, diag::err_dispose_done_not_destructor,
              {Pointee.Name, DtorName});
        for (const auto& A : DtorArgs) (void)checkExpr(*A);
        return;
    }
    // Turbo Tier 5, Cluster A item 7: same private-visibility gate
    // checkMethodCall applies -- see its own comment (SemaExpr.cpp).
    if (ML.M->IsPrivate && ML.Owner->DeclaringModule != CurrentUnit_)
        error(S.Args[1]->Loc, diag::err_object_private_method,
              {ML.Owner->Name, DtorName});

    Symbol Indirect;
    Indirect.Kind       = SymbolKind::Proc;
    Indirect.Name       = ML.Owner->Name + "." +
                           (ML.M->Heading ? ML.M->Heading->Name : DtorName);
    Indirect.IsFunction = ML.M->IsFunction;
    Indirect.Params     = ML.M->Params;
    Indirect.ReturnType = ML.M->RetType;
    (void)checkUserDefinedCall(Indirect, S.Args[1]->Loc, DtorArgs,
                               /*ExpectFunction=*/false);
    S.DisposeDoneMethod = DtorName;
}

void Sema::checkWith(const WithStmt& S) {
    // EP §6.4.7: save schema bindings before pushing (pushWithScope may add new ones).
    auto SavedBindings = ActiveSchemaBindings_;
    int Pushed = pushWithScope(S);
    checkStmt(S.Body.get());
    // Restore bindings (removes any schema discriminants added by pushWithScope).
    ActiveSchemaBindings_ = std::move(SavedBindings);
    // Issues #571/#623: ImplicitCallReceivers_ popped in the same lockstep
    // pushWithScope's own comment describes -- one entry per Symtab scope
    // this loop pushed, Object or not.
    for (int I = 0; I < Pushed; ++I) {
        Symtab.popScope();
        ImplicitCallReceivers_.pop_back();
    }
}

int Sema::pushWithScope(const WithStmt& S) {
    int Count = 0;
    for (const auto& Rec : S.Records) {
        auto T = checkExpr(*Rec);
        if (T->isError()) continue;

        // ISO §6.8.3.10 / EP §6.8.3.10: a with-statement's record-variable is
        // a variable-access, not any record-valued expression -- without this,
        // `with mk() do x := 5` for a function `mk` returning a record was
        // accepted, and the assignment to `x` landed in a temporary nothing
        // could ever read back.
        if (!isLValue(*Rec)) {
            error(Rec->Loc, diag::err_with_not_variable, {T->Name});
            continue;
        }

        // EP §6.4.2.5: a restricted type's components are not reachable by
        // any spelling of a component-access, and `with` opens exactly the
        // same access `.field` does -- rejectRestrictedComponent is the same
        // check checkField already applies there, so `with x do f := 1` on a
        // `restricted` record is refused exactly where `x.f := 1` already is,
        // rather than exposing every field a plain `.field` cannot reach.
        if (rejectRestrictedComponent(*Rec, *T)) continue;

        // EP §6.7.3.1: `with` opens a new spelling for the SAME storage a
        // protected parameter denotes, and checkNotProtected only ever asks
        // Symtab about the identifier actually written -- which, inside a
        // `with`, is the field's own with-scope symbol, not `r`.  Every field
        // exposed below is protected exactly when the record-access itself
        // is, or `with r do f := 5` on a `protected var r: rec` silently
        // wrote through it: `checkNotProtected("f")` found a fresh, never-
        // protected symbol and had nothing to say.
        Symbol* ProtectedVia = protectedBaseOf(*Rec);

        // EP §6.4.7: schema instance — expose discriminants and body record fields.
        // EP §6.4.7: an UNDISCRIMINATED schema -- `with p^ do` for a `^buf`.
        // Its fields are selectable by name like any record's; the difference
        // is that the discriminants are values carried by the object rather
        // than constants, so they are exposed as integer variables and NOT put
        // into ActiveSchemaBindings_, which exists for compile-time folding and
        // has no answer here.
        if (T->Kind == TypeKind::Schema && T->SchemaBody
                && schemaUnderlying(T->SchemaBody)->Kind == TypeKind::Record) {
            Symtab.pushScope(/*IsBlock=*/false);
            ++Count;
            // Issues #571/#623: a schema/record with-target has no methods
            // of its own to offer an implicit call -- see
            // ImplicitCallReceivers_'s own comment for why this still pushes
            // a (null) placeholder, kept in exact 1:1 lockstep with Count/
            // each Symtab.pushScope() call this loop makes.
            ImplicitCallReceivers_.push_back(nullptr);
            for (const auto& D : T->SchemaDiscs) {
                Symbol DS;
                // Const, as the discriminated branch below already has it.
                // Var here confused WHEN the value is known with WHETHER it may
                // be written: it is not known until run time and it may never
                // be written, and `with q^ do n := 99` was accepted because
                // this said otherwise.
                DS.Kind = SymbolKind::Const;
                DS.Name = D.Name;
                // EP §6.8.4: expose the discriminant's real declared type, the
                // same fallback checkField already uses, not a hardcoded
                // integer -- `with p^ do` for `Box(c: char)` must let `c`
                // compare against a char, not force every discriminant to
                // look like a plain integer.
                DS.Ty   = D.Ty && !D.Ty->isError() ? D.Ty : TyInt;
                (void)Symtab.define(std::move(DS));
            }
            // The body may itself be another schema instantiation (EP §6.4.7),
            // so the fields are the ones schemaUnderlying reaches, not the
            // immediate body's.
            for (const auto& F : schemaUnderlying(T->SchemaBody)->RecordFields) {
                Symbol FS;
                FS.Kind        = SymbolKind::Var;
                FS.Name        = F.Name;
                FS.Ty          = F.Ty;
                FS.IsProtected = ProtectedVia != nullptr;
                FS.Module      = ProtectedVia ? ProtectedVia->Module : std::string();
                (void)Symtab.define(std::move(FS));
            }
            continue;
        }
        if (T->Kind == TypeKind::SchemaInstance) {
            Symtab.pushScope(/*IsBlock=*/false);
            ++Count;
            // Issues #571/#623: see the identical placeholder push in the
            // undiscriminated-Schema branch just above.
            ImplicitCallReceivers_.push_back(nullptr);
            // Expose discriminants as constants of their declared type in both
            // the symbol table and ActiveSchemaBindings_ (for constBound
            // inside the with body).
            for (const auto& D : T->SchemaDiscs) {
                ActiveSchemaBindings_[toLower(D.Name)] = D.Value;
                Symbol DS;
                DS.Kind = SymbolKind::Const;
                DS.Name = D.Name;
                // EP §6.8.4: same fallback-to-declared-type idiom as
                // checkField -- see the undiscriminated-Schema branch above.
                DS.Ty   = D.Ty && !D.Ty->isError() ? D.Ty : TyInt;
                (void)Symtab.define(std::move(DS));
            }
            // If the underlying body is a record, also expose its fields.  The
            // body may itself be another schema instantiation, so this is
            // asked of schemaUnderlying, not the immediate SchemaBody.
            if (T->SchemaBody && schemaUnderlying(T->SchemaBody)->Kind == TypeKind::Record) {
                for (const auto& F : schemaUnderlying(T->SchemaBody)->RecordFields) {
                    Symbol FS;
                    FS.Kind        = SymbolKind::Var;
                    FS.Name        = F.Name;
                    FS.Ty          = F.Ty;
                    FS.IsProtected = ProtectedVia != nullptr;
                    FS.Module      = ProtectedVia ? ProtectedVia->Module : std::string();
                    (void)Symtab.define(std::move(FS));
                }
            }
            continue;
        }

        // Turbo Tier 5, Cluster A item 7: 'with anObjectInstance do' -- the
        // SAME plain-Record branch just below, reused unchanged for an
        // Object's own (already-flattened, see RecordFields's own comment,
        // Type.h) field list.  A field this module may not reach (private,
        // declared by a DIFFERENT module -- see err_object_private_field's
        // own comment) is simply left OUT of the with-scope rather than
        // exposed-then-flagged: nothing here re-checks a plain identifier
        // reference the way checkField does for 'x.Field', so the one
        // moment this loop has to decide reachability is now, and the field
        // not existing at all inside the with-body is what fpc's own
        // wording ("identifier idents no member") already reads as anyway.
        if (T->Kind == TypeKind::Object) {
            Symtab.pushScope(/*IsBlock=*/false);
            ++Count;
            // Issue #623: 'with anObjectInstance do' as an IMPLICIT-CALL
            // receiver too, so an unqualified method call inside this
            // with-block resolves -- see ImplicitCallReceivers_'s own
            // comment for the whole design.  Pushed AFTER Symtab.pushScope()
            // above, same ordering pushMethodSelfScope's own identical push
            // uses, so the two stacks stay in the same relative nesting
            // order everywhere both are consulted.
            ImplicitCallReceivers_.push_back(T);
            for (const auto& F : T->RecordFields) {
                if (F.IsPrivate && F.DeclaringModule != CurrentUnit_) continue;
                Symbol FS;
                FS.Kind = SymbolKind::Var;
                FS.Name = F.Name;
                FS.Ty   = F.Ty;
                (void)Symtab.define(std::move(FS));
            }
            continue;
        }

        if (T->Kind != TypeKind::Record) {
            error(Rec->Loc, diag::err_with_non_record, {T->Name});
            continue;
        }
        Symtab.pushScope(/*IsBlock=*/false);
        ++Count;
        // Issues #571/#623: see the identical placeholder push in the
        // undiscriminated-Schema branch above -- a plain record has no
        // methods of its own either.
        ImplicitCallReceivers_.push_back(nullptr);
        for (const auto& F : T->RecordFields) {
            Symbol FS;
            FS.Kind           = SymbolKind::Var;
            FS.Name           = F.Name;
            FS.Ty             = F.Ty;
            FS.FromPackedWith  = T->Packed;  // ISO §6.6.3.3: can't pass packed field as var
            FS.IsProtected    = ProtectedVia != nullptr;
            FS.Module         = ProtectedVia ? ProtectedVia->Module : std::string();
            (void)Symtab.define(std::move(FS));
        }
    }
    return Count;
}

void Sema::checkGoto(const GotoStmt& S) {
    Symbol* Sym = Symtab.lookup(S.Label);
    if (!Sym || Sym->Kind != SymbolKind::Label) {
        error(S.Loc, diag::err_goto_undefined, {S.Label});
        return;
    }
    // ISO §6.8.1: a goto may name a label of an enclosing block, and doing so
    // abandons every activation between here and the one that block belongs
    // to.  The label has to prefix a statement at the outermost level of that
    // block's statement part: landing anywhere else would resume in the middle
    // of a structured statement whose activation was just thrown away.
    if (!CurrentBlockLabels.contains(S.Label)) {
        // EP §6.11.2: unlike a program's block or an enclosing procedure's,
        // a module's 'to begin do'/'to end do' does not stay on the call
        // stack for as long as the module's procedures stay callable -- see
        // Symbol::LabelInModuleBlock.  Checked ahead of LabelNested: a label
        // at the outermost level of a module's statement part is exactly as
        // unreachable this way as one that is not.
        if (Sym->LabelInModuleBlock) {
            error(S.Loc, diag::err_goto_module_block, {S.Label});
            return;
        }
        if (Sym->LabelNested) {
            error(S.Loc, diag::err_goto_outer_block, {S.Label});
            return;
        }
        // -std=turbo (matching FPC): once the two structural checks above are
        // past, ISO 7185/EP accept this unconditionally -- it is exactly what
        // a non-local goto is, and LabelGotoEngine's setjmp/longjmp-shaped
        // machinery is what makes leaving every activation between here and
        // the block that declares the label actually work at run time.
        // Turbo has no such machinery and does not allow this at all: a goto
        // may only reach a label declared in the SAME routine's block.
        if (Opts.turbo()) {
            error(S.Loc, diag::err_goto_nonlocal_turbo, {S.Label});
            return;
        }
        Sym->LabelReferenced = true;
        return;
    }
    // ISO §6.8.1: a goto-statement shall not transfer control into a structured statement.
    // LabelEnclosingStmt was populated by the pre-scan (Phase 5.5) before this block's
    // Phase 6 started, so forward gotos (goto before label in source) are covered too.
    auto It = LabelEnclosingStmt.find(S.Label);
    if (It != LabelEnclosingStmt.end()) {
        // The label is inside a structured statement.  The goto is only valid if we are
        // currently inside that same structured statement (i.e., its pointer appears in
        // StructStack).
        if (!std::ranges::contains(StructStack, It->second))
            error(S.Loc, diag::err_goto_into_structured, {S.Label});
    }
    Sym->LabelReferenced = true;
}

void Sema::checkLabeled(const LabeledStmt& S) {
    Symbol* Sym = Symtab.lookup(S.Label);
    if (!Sym || Sym->Kind != SymbolKind::Label) {
        error(S.Loc, diag::err_label_not_declared, {S.Label});
    } else if (!CurrentBlockLabels.contains(S.Label)) {
        // §6.1.6: a label-declaration-part declares the labels of the
        // statements of *that* block.  Symtab.lookup answers by spelling and
        // reaches enclosing scopes, so without this an inner block's labelled
        // statement satisfied an outer block's declaration -- and the landing
        // place was then planted in the declaring block's function, where
        // nothing ever jumps to it and the basic block has no terminator.
        // checkGoto is deliberately untouched: naming an enclosing block's
        // label is what a non-local goto is.  It is the statement that has to
        // stay home, not the jump.
        error(S.Loc, diag::err_label_wrong_block, {S.Label});
        // Counted as placed and reached so the declaring block does not go on
        // to report it as never defined and never jumped to: one mistake, one
        // diagnostic.  Both flags are read only by the Phase 7.5 audit.
        Sym->LabelPlaced = Sym->LabelReferenced = true;
    } else {
        Sym->LabelPlaced = true;
    }
    checkStmt(S.Stmt.get());
}

void Sema::checkCase(const CaseStmt& S) {
    auto SelType = checkExpr(*S.Selector);
    if (!SelType->isError() && !SelType->isOrdinal())
        error(S.Loc, diag::err_case_selector_not_ordinal, {SelType->Name});

    // §6.8.3.5: the case-constants shall be distinct.  Two arms holding the
    // same value give the statement two meanings, and the one the generated
    // code picks would be an accident of how the arms were laid out.
    // Named back in the terms the label was written in: the ordinal of a char
    // or an enumeration is not what the reader put in the program, and asking
    // them to work out which label '97' was is asking them to do the work
    // twice.
    auto spell = [&](int64_t V) { return spellOrdinal(*SelType, V); };

    // Duplicate/overlap detection is done on [Lo, Hi] intervals rather than
    // by enumerating every value a range-shaped label denotes: a label like
    // `1..100000000` is a single interval to compare, not a hundred million
    // values to insert into a set.  Singleton labels are just the interval
    // [V, V].  This keeps the check O(labels^2), not O(sum of range spans),
    // regardless of how wide any individual range is.
    struct LabelInterval { int64_t Lo, Hi; };
    std::vector<LabelInterval> Seen;
    auto overlapsSeen = [&](int64_t Lo, int64_t Hi) {
        return std::ranges::any_of(Seen, [&](const LabelInterval& R) {
            return Lo <= R.Hi && R.Lo <= Hi;
        });
    };
    auto noteInterval = [&](int64_t Lo, int64_t Hi, const ExprNode& E) {
        if (overlapsSeen(Lo, Hi))
            error(E.Loc, diag::err_case_label_duplicate, {spell(Lo)});
        Seen.push_back({Lo, Hi});
    };

    for (const auto& Arm : S.Arms) {
        for (const auto& Lbl : Arm.Labels) {
            auto checkLabel = [&](const ExprNode& E) {
                auto T = checkExpr(E);
                if (!SelType->isError() && !T->isError()
                    && !isAssignCompatible(*SelType, *T)
                    && !isAssignCompatible(*T, *SelType))
                    error(E.Loc, diag::err_case_label_mismatch,
                          {T->Name, SelType->Name});
            };
            checkLabel(*Lbl.Low);
            if (Lbl.High) checkLabel(*Lbl.High);

            // ISO §6.8.3.5: a case-label is a case-CONSTANT.  This used to
            // fold only to find duplicates and skip quietly when it could not,
            // so a label that was not constant reached codegen and lowered to a
            // load of the variable: `case i of 1..n:` compared the selector
            // against whatever n held at that moment.  An illegal program
            // compiled into a plausible-looking one.
            auto mustBeConst = [&](const ExprNode& E,
                                   const std::optional<int64_t>& V) {
                // Quiet where the label was already reported as ill-typed;
                // one mistake should not be told twice.
                if (!V && E.ResolvedType && !E.ResolvedType->isError())
                    error(E.Loc, diag::err_case_label_not_const);
            };
            // A range stands for every value in it, and any of them can be the
            // one another arm repeats.
            const auto Lo = constBound(*Lbl.Low);
            mustBeConst(*Lbl.Low, Lo);
            const auto Hi = Lbl.High ? constBound(*Lbl.High) : Lo;
            if (Lbl.High) mustBeConst(*Lbl.High, Hi);
            if (!Lo || !Hi || *Hi < *Lo) continue;
            noteInterval(*Lo, *Hi, *Lbl.Low);
        }
        checkStmt(Arm.Body.get());
    }
    if (S.Else) checkStmt(S.Else.get());
    // `case i of 1: f otherwise end` is the idiom for "and nothing for the
    // rest", so it is the part being written that answers for the values the
    // arms miss, not the part having a statement in it.
    if (S.HasElse) return;

    // Turbo's case is not exhaustive-or-die (see CGControlFlow::emitCase):
    // an unmatched selector with no else/otherwise part falls through
    // instead of trapping, so this warning's premise -- that reaching such a
    // value "is an error" -- is simply false under this dialect.  Falling
    // through with no else is the idiomatic, expected Turbo pattern, not a
    // mistake worth flagging, and unlike an ordinary warning this is gated
    // before the call below rather than left to -Wno-case-not-exhaustive: no
    // -W flag can reintroduce a warning whose call site never runs.
    if (Opts.turbo()) return;

    // §6.8.3.5: reaching a case-statement with a selector value that matches no
    // case-constant is an error, reported when the program runs.  Where the
    // selector's type is small enough to enumerate, the values that would go
    // unmatched can be named before it does.  An 'otherwise' part answers for
    // all of them, which is why this only runs when there is none.
    if (SelType->isError() || !SelType->isOrdinal()) return;
    // Only a range the program wrote down counts as one it meant to cover.
    // Every char is within chr(0)..chr(255) whether the author thought about
    // it or not, and asking a `case c of 'a'..'z'` to account for the other
    // 230 values would be a warning nobody would keep switched on.
    if (SelType->Kind != TypeKind::Subrange && SelType->Kind != TypeKind::Enum
        && SelType->Kind != TypeKind::Boolean)
        return;
    auto R = ordinalRange(*SelType);
    if (!R) return;                      // integer: nothing to be exhaustive over
    const auto [Lo, Hi] = *R;
    if (Lo > Hi) return;
    // Bounds the walk below, and keeps the warning to types a reader could
    // reasonably be expected to enumerate by hand.
    constexpr int64_t MostToEnumerate = 1024;
    if (Hi - Lo + 1 > MostToEnumerate) return;

    std::vector<int64_t> Missing;
    int64_t MissingCount = 0;
    for (int64_t V = Lo; V <= Hi; ++V) {
        if (!overlapsSeen(V, V)) {
            ++MissingCount;
            if (Missing.size() < 3) Missing.push_back(V);
        }
        if (V == Hi) break;              // Hi may be the largest int64_t
    }
    if (MissingCount == 0) return;

    std::string What;
    for (size_t I = 0; I < Missing.size(); ++I) {
        if (I) What += (I + 1 == Missing.size() && MissingCount == static_cast<int64_t>(Missing.size()))
                           ? " or " : ", ";
        What += spell(Missing[I]);
    }
    if (MissingCount > static_cast<int64_t>(Missing.size()))
        What += std::format(" (and {} more)",
                            MissingCount - static_cast<int64_t>(Missing.size()));
    warning(S.Loc, diag::warn_case_not_exhaustive, {What, SelType->Name});
}

// EP §6.9.3.9.3: for v in set-expr do stmt
void Sema::checkForIn(const ForInStmt& S) {
    auto SetTy = checkExpr(*S.SetExpr);
    if (!SetTy->isError() && SetTy->Kind != TypeKind::Set)
        error(S.Loc, diag::err_for_in_not_set, {SetTy->Name});

    // The set's element type (or integer if we can't determine it), used
    // below to check the DECLARED control variable's type.
    auto ElemTy = (SetTy->Kind == TypeKind::Set && SetTy->ElemType)
                  ? SetTy->ElemType : TyInt;

    // ISO 10206 §6.9.3.9.1 (issue #689): `for v in set do` names an ordinary
    // variable-access as its control variable -- exactly like checkFor's
    // to/downto form (ISO §6.8.3.9) -- not one this loop declares on v's
    // behalf.  This used to push a fresh scope and implicitly `define` v
    // there every time, which (a) let an UNDECLARED v compile silently,
    // (b) never checked a declared v's type against the set's element type,
    // (c) made a declared v's only use (driving the loop) invisible to the
    // "declared but never used" check, since the shadow, not the real
    // symbol, was the one marked Referenced, and (d) was internally
    // inconsistent with CodeGen (CGControlFlow::emitForIn), which already
    // looks up and writes through the OUTER declared variable when one
    // exists -- so the value Sema treated as scoped to the loop body in
    // fact leaked past it at run time.
    Symbol* Sym = Symtab.lookup(S.Var);
    if (!Sym) {
        error(S.Loc, diag::err_for_var_undefined, {S.Var});
    } else {
        // Driving a loop is a use, the same as checkFor's identical note.
        Sym->Referenced = true;
        const bool IsVar = Sym->Kind == SymbolKind::Var
                         || Sym->Kind == SymbolKind::VarParam;
        if (!IsVar) {
            error(S.Loc, diag::err_for_var_not_variable, {S.Var});
        } else {
            if (!SetTy->isError() && !isAssignCompatible(*Sym->declaredType(), *ElemTy))
                error(S.Loc, diag::err_for_in_var_wrong_type,
                      {S.Var, Sym->declaredType()->Name, ElemTy->Name});
            // ISO §6.8.3.9 / §6.9.3.9.1: local to the immediately enclosing
            // block -- see checkFor's identical check for why
            // lookupInEnclosingBlock, not lookup, is what "local" means.
            if (!Symtab.lookupInEnclosingBlock(S.Var))
                error(S.Loc, diag::err_for_var_not_local, {S.Var});
        }
    }

    // ISO §6.8.3.9's control-variable restrictions (no reassignment,
    // var-parameter aliasing, or use as a read/readln/readstr/writestr
    // target within the loop body) are the same for `for v in set do` as
    // for `to`/`downto` -- #265/#291 (PR #342) wired checkForBody up for
    // the latter but never for this form, leaving every one of those
    // threats silently unchecked here.
    checkForBody(S.Body.get(), S.Var, S.Loc);
    {
        LoopScope LS(LoopDepth_);
        checkStmt(S.Body.get());
    }
}
