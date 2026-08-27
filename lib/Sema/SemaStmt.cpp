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
static_assert(NumStmtKinds == 12, "a new statement needs a case in checkStmt");

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
// the halt that Extended Pascal adds.  Everything else completes, so a
// statement following one of those is reached by nothing — unless it carries a
// label, which puts it back within reach of any goto that names it.
//
// The analysis errs one way on purpose.  Treating a statement as completing
// when it does not costs a warning that was not given; the opposite would call
// live code dead, and a warning that is wrong is worse than one that is
// missing.
// ---------------------------------------------------------------------------

namespace {

bool sequenceTransfers(const std::vector<std::unique_ptr<StmtNode>>& Stmts,
                        const SymbolTable& Symtab);

// Whether S calls the real halt builtin -- resolved to the symbol it names,
// not matched on how that name is spelled.  A program is free to declare its
// own procedure called `halt`, which shadows the builtin (ISO §6.2.2.10) and
// returns like any other call; only the builtin itself never does.  `exit`
// gets no such check at all: Builtins.def has no entry for it, so a call
// spelled `exit` can only ever resolve to a declaration the program wrote,
// never to a required procedure that leaves for good.
bool callsHaltBuiltin(const CallStmt& C, const SymbolTable& Symtab) {
    const Symbol* Callee = Symtab.lookup(C.Name);
    return Callee && Callee->Kind == SymbolKind::Builtin
                   && Callee->BuiltinKind == BuiltinID::Halt;
}

bool alwaysTransfers(const StmtNode* S, const SymbolTable& Symtab) {
    if (!S) return false;
    if (llvm::isa<GotoStmt>(S)) return true;
    if (auto* C = llvm::dyn_cast<CallStmt>(S)) return callsHaltBuiltin(*C, Symtab);
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
    const Symbol* Sym = Symtab.lookup(Name);
    const bool ShadowedByPlainVar = Sym
        && (Sym->Kind == SymbolKind::Var || Sym->Kind == SymbolKind::VarParam)
        && !Sym->IsResultVar;
    const bool Shadowed = Sym && (Sym->Kind == SymbolKind::Var
                                  || Sym->Kind == SymbolKind::VarParam);

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
    // The target is resolved before it is judged, because whether it is
    // assignable can depend on what it turned out to be: `v.n` is a field in
    // the syntax and a schema discriminant in the type, and only the second
    // says it cannot be written to.
    auto Dst = checkExpr(*S.Target);
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
            if (!Sym->Ty->isOrdinal())
                error(S.Loc, diag::err_for_var_not_ordinal, {S.Var, Sym->Ty->Name});

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

    // ISO §6.8.3.9: the body must not threaten the control variable (assign to it).
    checkForBody(S.Body.get(), S.Var, S.Loc);
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

// Unified for-loop threat scanner (ISO §6.8.3.9).
// When `Callables` is null the symbol table resolves var-param flags (intra-procedural).
// When `Callables` is non-null the symbol table scope is already closed; the supplied
// AST proc list is used instead (inter-procedural mode).
void Sema::checkForBody(const StmtNode* Stmt, const std::string& VarName,
                         SourceLocation ForLoc,
                         const std::vector<const ProcDecl*>* Callables) {
    const bool InterProc = Callables != nullptr;
    walkStmts(Stmt, [&](const StmtNode* S) {
        if (auto* A = llvm::dyn_cast<AssignStmt>(S)) {
            if (auto* Id = llvm::dyn_cast<IdentExpr>(A->Target.get()))
                if (eqCI(Id->Name, VarName))
                    error(A->Loc,
                          InterProc ? diag::err_for_inter_assigns
                                    : diag::err_for_body_assigns,
                          {VarName});
        } else if (!InterProc) {
            // Inner for-loop reuse: only checked in the direct (intra-procedural) scan.
            if (auto* Fs = llvm::dyn_cast<ForStmt>(S))
                if (eqCI(Fs->Var, VarName))
                    error(Fs->Loc, diag::err_for_body_reuses, {VarName});
        }
        if (auto* Cs = llvm::dyn_cast<CallStmt>(S)) {
            std::string Lo = toLower(Cs->Name);
            if ((Lo == "read" || Lo == "readln") && !Cs->Args.empty()) {
                for (const auto& Arg : Cs->Args) {
                    if (auto* Id = llvm::dyn_cast<IdentExpr>(Arg.get()))
                        if (eqCI(Id->Name, VarName))
                            error(Cs->Loc,
                                  InterProc ? diag::err_for_inter_reads
                                            : diag::err_for_body_reads,
                                  {VarName});
                }
            }
            if (InterProc) {
                // Resolve var-param flags from AST proc list (symbol table scope is closed).
                for (const ProcDecl* Pd : *Callables) {
                    if (!eqCI(Pd->Name, Cs->Name)) continue;
                    size_t ArgIdx = 0;
                    for (const auto& Pg : Pd->Params) {
                        for (size_t K = 0; K < Pg.Names.size(); ++K, ++ArgIdx) {
                            if (ArgIdx >= Cs->Args.size()) break;
                            if (!Pg.IsVar) continue;
                            if (auto* Id = llvm::dyn_cast<IdentExpr>(Cs->Args[ArgIdx].get()))
                                if (eqCI(Id->Name, VarName))
                                    error(Cs->Loc, diag::err_for_inter_var_param, {VarName});
                        }
                    }
                    break;
                }
            } else {
                // Resolve var-param flags from the symbol table (intra-procedural).
                Symbol* Callee = Symtab.lookup(Cs->Name);
                if (Callee && (Callee->Kind == SymbolKind::Proc || Callee->Kind == SymbolKind::Builtin)) {
                    for (size_t I = 0; I < Callee->Params.size() && I < Cs->Args.size(); ++I) {
                        if (!Callee->Params[I].IsVar) continue;
                        if (auto* Id = llvm::dyn_cast<IdentExpr>(Cs->Args[I].get()))
                            if (eqCI(Id->Name, VarName))
                                error(Cs->Loc, diag::err_for_body_var_param, {VarName});
                    }
                }
            }
        }
    });
}

void Sema::checkRepeat(const RepeatStmt& S) {
    warnUnreachable(S.Stmts);
    for (const auto& St : S.Stmts) checkStmt(St.get());
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
        break;
    }
    auto* Id = llvm::dyn_cast<IdentExpr>(Base);
    if (!Id) return nullptr;
    Symbol* Sym = Symtab.lookup(Id->Name);
    // EP §6.7.3.7.1 NOTE 2: a conformant-array bound identifier is refused an
    // assignment the same way a protected parameter is -- see
    // checkNotProtected, which tells the two apart for the diagnostic.
    return (Sym && (Sym->IsProtected || Sym->IsConformantBound)) ? Sym : nullptr;
}

void Sema::checkNotProtected(const ExprNode& Target, SourceLocation Loc) {
    Symbol* Sym = protectedBaseOf(Target);
    if (!Sym) return;
    if (Sym->IsConformantBound) {
        error(Loc, diag::err_conformant_bound_assigned, {Sym->Name});
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
        if (Sym->IsFunction) {
            error(S.Loc, diag::err_func_as_statement, {S.Name});
            for (const auto& Arg : S.Args) (void)checkExpr(*Arg);
            return;
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

        // §6.9.4: page, like readln and writeln, is about lines, and only a
        // text file has any.  The other two are checked where their arguments
        // are already being walked, since checking an expression twice reports
        // anything wrong with it twice.
        if (Lo == "page" && !S.Args.empty()) {
            auto T = checkExpr(*S.Args[0]);
            if (T->Kind == TypeKind::File && T->ElemType
                && T->ElemType->Kind != TypeKind::Char)
                error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                      {Lo, T->ElemType->Name});
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
            if (T->Kind == TypeKind::File && T->ElemType
                && T->ElemType->Kind != TypeKind::Char) {
                for (size_t I = 1; I < S.Args.size(); ++I) {
                    auto At = checkExpr(*S.Args[I]);
                    if (!At->isError() && !T->ElemType->isError()
                        && !isAssignCompatible(*T->ElemType, *At))
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
                    // lines to end.
                    if (Lo == "writeln" && T->ElemType
                            && T->ElemType->Kind != TypeKind::Char)
                        error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                              {Lo, T->ElemType->Name});
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
                    break;
                default:
                    // ISO §6.4.3.2: a packed array[1..n] of char is a string
                    // value, and §6.9.3.1 admits string values.
                    if (isCharStringType(*T)) break;
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
            const bool FromText = !HasFile || !Ts[0]->ElemType
                                  || Ts[0]->ElemType->Kind == TypeKind::Char;

            for (size_t I = 0; I < Ts.size(); ++I) {
                const auto& T = *Ts[I];
                if (T.isRestricted())
                    error(S.Args[I]->Loc, diag::err_restricted_used, {T.Name});
                else if (!(HasFile && I == 0) && FromText && !T.isError())
                    checkReadParamType(T, S.Args[I]->Loc);
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
                if (T && T->Kind == TypeKind::File && T->ElemType
                    && T->ElemType->Kind != TypeKind::Char)
                    error(S.Args[0]->Loc, diag::err_line_proc_not_text,
                          {Lo, T->ElemType->Name});
            }
            return;
        }

        // read: if first arg is a typed file, remaining args must match element type.
        if (Lo == "read" && !S.Args.empty()) {
            auto T = checkExpr(*S.Args[0]);
            if (T->Kind == TypeKind::File && T->ElemType && S.Args.size() >= 2) {
                for (size_t I = 1; I < S.Args.size(); ++I) {
                    auto At = checkExpr(*S.Args[I]);
                    if (!At->isError() && !T->ElemType->isError()
                        && !isAssignCompatible(*At, *T->ElemType))
                        error(S.Args[I]->Loc, diag::err_read_type_mismatch,
                              {T->ElemType->Name, At->Name});
                }
            } else {
                for (size_t I = 1; I < S.Args.size(); ++I) (void)checkExpr(*S.Args[I]);
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
            if (!PkdTy->isError() && PkdTy->Kind != TypeKind::Array)
                error(PkdArg.Loc, diag::err_pack_operand_not_array_packed,
                      {Lo, PkdTy->Name});
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
            if (!ToSchema && S.Args.size() > 1 && Pointee && !Pointee->isError()
                    && !(Pointee->Kind == TypeKind::Record && Pointee->RecordDecl
                         && Pointee->RecordDecl->Variant))
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

        // EP §6.7.5.6: bind and unbind reach past the value of their first
        // argument to the variable itself, so what it may be is fixed.
        if (Lo == "bind" || Lo == "unbind") {
            checkBindingCall(Lo, S.Loc, S.Args);
            return;
        }

        // Generic: arity was already checked against Builtins.def above.
        // Evaluate arguments for side-effects / type errors and leave the
        // rest -- which argument means what -- to codegen.
        for (const auto& Arg : S.Args) (void)checkExpr(*Arg);
        return;
    }
    (void)checkUserDefinedCall(*Sym, S.Loc, S.Args, /*expectFunction=*/false);
}

void Sema::checkWith(const WithStmt& S) {
    // EP §6.4.7: save schema bindings before pushing (pushWithScope may add new ones).
    auto SavedBindings = ActiveSchemaBindings_;
    int Pushed = pushWithScope(S);
    checkStmt(S.Body.get());
    // Restore bindings (removes any schema discriminants added by pushWithScope).
    ActiveSchemaBindings_ = std::move(SavedBindings);
    for (int I = 0; I < Pushed; ++I) Symtab.popScope();
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

        if (T->Kind != TypeKind::Record) {
            error(Rec->Loc, diag::err_with_non_record, {T->Name});
            continue;
        }
        Symtab.pushScope(/*IsBlock=*/false);
        ++Count;
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

    // The loop variable is implicitly declared for the duration of the body.
    // It has the element type of the set (or integer if we can't determine it).
    auto ElemTy = (SetTy->Kind == TypeKind::Set && SetTy->ElemType)
                  ? SetTy->ElemType : TyInt;

    Symtab.pushScope(/*IsBlock=*/false);
    Symbol LoopSym;
    LoopSym.Kind  = SymbolKind::Var;
    LoopSym.Name  = S.Var;
    LoopSym.Ty    = ElemTy;
    LoopSym.DeclLoc = S.Loc;
    if (!Symtab.define(LoopSym))
        error(S.Loc, diag::err_for_in_var_scope, {S.Var});
    checkStmt(S.Body.get());
    Symtab.popScope();
}
