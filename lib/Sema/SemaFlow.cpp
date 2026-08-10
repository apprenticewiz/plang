// SemaFlow.cpp — definite-assignment analysis.
//
// ISO 7185 §6.5.1 says a variable has no value until one is assigned to it,
// and §5.1 f) 1) lets a processor leave the reading of such a variable
// unreported provided it says so.  plang says so, in docs/conformance.md: the
// value is whatever the storage held.  What follows narrows that admission by
// reporting the cases a walk over the block can be sure of.
//
// Two other rules fall out of the same walk, because both are about a variable
// having no value at a particular point rather than at any point:
//
//   §6.8.3.9  after a for-statement its control variable is undefined again.
//   §6.6.2    the value of a function is the last assigned to its result, so a
//             path assigning nothing leaves the result whatever storage held.
//
// WHAT THE WALK WILL NOT DO
// -------------------------
// It reports a read only where no path reaching it assigns the variable, so
// branches merge by intersection and a loop body is entered knowing only what
// held before the loop.  Where the flow stops being followable the whole block
// is abandoned: a label can be reached by any goto that names it, and after
// that the walk would be guessing.  Likewise a variable a nested procedure can
// reach is dropped, since the assignment may be in there.
//
// The bias is deliberate and one-way.  Missing a warning costs a warning; a
// warning about correct code costs the reader's trust in all the others.

#include "plang/Sema/Sema.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SemaUtil.h"
#include "plang/Basic/StringUtil.h"

#include "llvm/Support/Casting.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>

using namespace plang;

namespace {

/// The variable an access is ultimately of: `a[i].f^` is an access of `a`.
/// Returns null when the access is not rooted in a plain identifier.
const IdentExpr* rootVariable(const ExprNode* E) {
    while (E) {
        if (auto* N = llvm::dyn_cast<IdentExpr>(E))     return N;
        if (auto* N = llvm::dyn_cast<IndexExpr>(E))     { E = N->Array.get();   continue; }
        if (auto* N = llvm::dyn_cast<FieldExpr>(E))     { E = N->Record.get();  continue; }
        if (auto* N = llvm::dyn_cast<DerefExpr>(E))     { E = N->Pointer.get(); continue; }
        if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) { E = N->Str.get();     continue; }
        return nullptr;
    }
    return nullptr;
}

/// Whether a value of this type is one the walk can follow.  A structured
/// variable is given its value a component at a time, so "has it been
/// assigned" has no single answer for one, and a file is opened rather than
/// assigned.  Restricting the walk to the types that hold one value keeps it
/// from reporting a record that was filled in field by field.
bool isSimpleValue(const Type* T) {
    if (!T) return false;
    switch (T->Kind) {
    case TypeKind::Integer: case TypeKind::Real:    case TypeKind::Boolean:
    case TypeKind::Char:    case TypeKind::Enum:    case TypeKind::Subrange:
    case TypeKind::Pointer: case TypeKind::Complex:
        return true;
    default:
        return false;
    }
}

/// Whether the flow through this block can be followed at all.  A goto may
/// land on any label in scope, so a block using them is one where "the paths
/// reaching this statement" is not a question the walk can answer.
bool flowIsFollowable(const BlockNode& Block) {
    if (!Block.Labels.empty()) return false;
    bool Ok = true;
    walkStmts(Block.Body.get(), [&](const StmtNode* S) {
        if (llvm::isa<GotoStmt>(S) || llvm::isa<LabeledStmt>(S)) Ok = false;
    });
    return Ok;
}

/// Every identifier named anywhere inside these procedures, including the ones
/// nested within them.  A variable of the enclosing block that appears in here
/// may be assigned out of the walk's sight, so it cannot be tracked.
void collectNamesUsedIn(const std::vector<std::unique_ptr<ProcDecl>>& Procs,
                        std::set<std::string>& Out) {
    for (const auto& P : Procs) {
        if (!P->Body) continue;
        walkStmts(P->Body->Body.get(), [&](const StmtNode* S) {
            if (auto* C = llvm::dyn_cast<CallStmt>(S)) Out.insert(toLower(C->Name));
            forEachStmtExpr(S, [&](const ExprNode* E) {
                walkExprs(E, [&](const ExprNode* X) {
                    if (auto* Id = llvm::dyn_cast<IdentExpr>(X))
                        Out.insert(toLower(Id->Name));
                });
            });
        });
        collectNamesUsedIn(P->Body->Procs, Out);
    }
}

/// The variables assigned on both of two paths — what is still known once they
/// meet.  Everything else may or may not have a value, which is the same as
/// not knowing that it has one.
std::set<std::string> intersect(const std::set<std::string>& A,
                                const std::set<std::string>& B) {
    std::set<std::string> Out;
    std::ranges::set_intersection(A, B, std::inserter(Out, Out.end()));
    return Out;
}

/// The union, used for the two sets where not knowing is the safe answer:
/// a variable left undefined on either path is one the walk must warn about.
std::set<std::string> unite(const std::set<std::string>& A,
                            const std::set<std::string>& B) {
    std::set<std::string> Out = A;
    Out.insert(B.begin(), B.end());
    return Out;
}

} // namespace

void Sema::FlowState::mergeWith(const FlowState& Other) {
    Assigned       = intersect(Assigned, Other.Assigned);
    UndefAfterFor  = unite(UndefAfterFor, Other.UndefAfterFor);
    ResultAssigned = ResultAssigned && Other.ResultAssigned;
}

// ---------------------------------------------------------------------------
// Reads and writes
// ---------------------------------------------------------------------------

void Sema::flowRead(const ExprNode* E, FlowState& St) {
    if (!E) return;
    walkExprs(E, [&](const ExprNode* X) {
        // An access is a read of the variable it is rooted in.  The subscript
        // of an index and the like are reached by the walk in their own right,
        // so only the root has to be named here.
        const IdentExpr* Id = nullptr;
        if (llvm::isa<IdentExpr>(X))       Id = llvm::cast<IdentExpr>(X);
        else if (llvm::isa<IndexExpr>(X) || llvm::isa<FieldExpr>(X)
              || llvm::isa<DerefExpr>(X)  || llvm::isa<SubstringExpr>(X))
            Id = rootVariable(X);
        if (!Id) return;

        const std::string Key = toLower(Id->Name);
        if (!FlowTracked_.contains(Key))  return;
        if (St.Assigned.contains(Key))    return;
        if (FlowReported_.contains(Key))  return;

        // §6.8.3.9 is a rule of its own and reads as one: the variable did
        // have a value, and the for-statement ending is what took it away.
        // Saying only "never given a value" would be wrong as well as unhelpful.
        if (St.UndefAfterFor.contains(Key)) {
            warning(Id->Loc, diag::warn_for_var_after_loop, {Id->Name});
        } else {
            warning(Id->Loc, diag::warn_var_uninitialized, {Id->Name});
        }
        FlowReported_.insert(Key);
    });
}

void Sema::flowWrite(const ExprNode* E, FlowState& St) {
    const IdentExpr* Id = rootVariable(E);
    if (!Id) return;
    const std::string Key = toLower(Id->Name);

    // Assigning the function's result is what §6.6.2 asks about, and the name
    // is not a variable of the block, so it is answered separately.
    if (FlowResultNames_.contains(Key)) { St.ResultAssigned = true; return; }

    if (!FlowTracked_.contains(Key)) return;
    // A component assignment gives the whole variable a value as far as this
    // walk is concerned.  Only simple variables are tracked, so in practice
    // the access is the variable itself.
    St.Assigned.insert(Key);
    St.UndefAfterFor.erase(Key);
}

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

void Sema::flowSeq(const std::vector<std::unique_ptr<StmtNode>>& Stmts,
                   FlowState& St) {
    for (const auto& S : Stmts) flowStmt(S.get(), St);
}

void Sema::flowStmt(const StmtNode* S, FlowState& St) {
    if (!S) return;

    if (auto* N = llvm::dyn_cast<CompoundStmt>(S)) {
        flowSeq(N->Stmts, St);
        return;
    }

    if (auto* N = llvm::dyn_cast<AssignStmt>(S)) {
        // The value is read first, and so is anything the target is subscripted
        // or dereferenced by — `a[i] := 0` reads i.  What the target does not
        // do is read the variable it names.
        flowRead(N->Value.get(), St);
        if (!llvm::isa<IdentExpr>(N->Target.get()))
            if (const IdentExpr* Root = rootVariable(N->Target.get())) {
                walkExprs(N->Target.get(), [&](const ExprNode* X) {
                    if (X != Root && !llvm::isa<IdentExpr>(X)) return;
                    if (X == Root) return;
                    flowRead(X, St);
                });
                // A dereference or a subscript reads the variable it is of:
                // p^ := 0 needs p to point somewhere.
                flowRead(Root, St);
            }
        flowWrite(N->Target.get(), St);
        return;
    }

    if (auto* N = llvm::dyn_cast<CallStmt>(S)) {
        // A var parameter is where the callee puts its answer, so an argument
        // passed to one is written rather than read.
        const std::string Lo = toLower(N->Name);
        const Symbol* Callee = Symtab.lookup(N->Name);
        const bool IsBuiltin = Callee && Callee->Kind == SymbolKind::Builtin;

        // The standard procedures are declared without parameter lists, since
        // most of them are variadic, so the ones that give a variable a value
        // have to be named.  read and readln do it to everything they are
        // given — the file they may start with is not a variable this walk
        // tracks — and new does it to the pointer.  The rest only look.
        auto builtinAssigns = [&](size_t I) {
            if (Lo == "read" || Lo == "readln") return true;
            if (Lo == "new")                    return I == 0;
            return false;
        };

        for (size_t I = 0; I < N->Args.size(); ++I) {
            bool ByRef;
            if (IsBuiltin)                                ByRef = builtinAssigns(I);
            else if (Callee && I < Callee->Params.size()) ByRef = Callee->Params[I].IsVar;
            // A signature the walk cannot read is assumed to assign, which
            // costs a warning rather than inventing one.
            else                                          ByRef = true;
            if (ByRef) flowWrite(N->Args[I].get(), St);
            else       flowRead (N->Args[I].get(), St);
        }
        return;
    }

    if (auto* N = llvm::dyn_cast<IfStmt>(S)) {
        flowRead(N->Cond.get(), St);
        FlowState Then = St;
        flowStmt(N->Then.get(), Then);
        if (N->Else) {
            FlowState Else = St;
            flowStmt(N->Else.get(), Else);
            Then.mergeWith(Else);
            St = std::move(Then);
        } else {
            // Without an else the other path is the one that does nothing, and
            // it assigns nothing, so nothing new is known.
            St.UndefAfterFor = unite(St.UndefAfterFor, Then.UndefAfterFor);
        }
        return;
    }

    if (auto* N = llvm::dyn_cast<WhileStmt>(S)) {
        flowRead(N->Cond.get(), St);
        // The body is entered with what held before the loop, which is right
        // for the first time round and is the only iteration that can read a
        // variable nothing has assigned yet.  It may run no times, so what it
        // assigns is not known afterwards.
        FlowState Body = St;
        flowStmt(N->Body.get(), Body);
        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        return;
    }

    if (auto* N = llvm::dyn_cast<RepeatStmt>(S)) {
        // A repeat runs its body at least once, so what the body assigns is
        // assigned afterwards, and the condition is read after the body.
        flowSeq(N->Stmts, St);
        flowRead(N->Cond.get(), St);
        return;
    }

    if (auto* N = llvm::dyn_cast<ForStmt>(S)) {
        flowRead(N->From.get(), St);
        flowRead(N->Limit.get(), St);
        const std::string Key = toLower(N->Var);

        FlowState Body = St;
        Body.Assigned.insert(Key);          // the control variable has a value inside
        Body.UndefAfterFor.erase(Key);
        flowStmt(N->Body.get(), Body);

        // The loop may run no times, so nothing the body assigned is known.
        // §6.8.3.9: and the control variable is undefined once it finishes.
        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        St.Assigned.erase(Key);
        if (FlowTracked_.contains(Key)) St.UndefAfterFor.insert(Key);
        return;
    }

    if (auto* N = llvm::dyn_cast<ForInStmt>(S)) {
        flowRead(N->SetExpr.get(), St);
        FlowState Body = St;
        flowStmt(N->Body.get(), Body);
        St.UndefAfterFor = unite(St.UndefAfterFor, Body.UndefAfterFor);
        return;
    }

    if (auto* N = llvm::dyn_cast<CaseStmt>(S)) {
        flowRead(N->Selector.get(), St);
        // §6.8.3.5: a selector matching no arm is an error, and plang reports
        // it when the program runs.  So every path that carries on past the
        // case went through an arm, and what all the arms assign is assigned.
        std::optional<FlowState> Merged;
        auto takeBranch = [&](const StmtNode* Body) {
            FlowState Br = St;
            flowStmt(Body, Br);
            if (Merged) Merged->mergeWith(Br);
            else        Merged = std::move(Br);
        };
        for (const auto& Arm : N->Arms) takeBranch(Arm.Body.get());
        if (N->HasElse) takeBranch(N->Else.get());
        if (Merged) St = std::move(*Merged);
        return;
    }

    if (auto* N = llvm::dyn_cast<WithStmt>(S)) {
        for (const auto& R : N->Records) flowRead(R.get(), St);
        flowStmt(N->Body.get(), St);
        return;
    }

    // A labelled statement or a goto means flowIsFollowable said no and the
    // walk was never started.  Anything else has no expressions and no body.
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void Sema::checkDefiniteAssignment(const BlockNode& Block) {
    if (!Block.Body) return;
    // A program that does not typecheck is not one whose flow means anything.
    // The walk reads a tree where some names have no type and some statements
    // were kept only to carry on looking for errors, and what it would say
    // about that is guesswork stacked on a mistake the reader already has to
    // fix.
    if (std::ranges::any_of(Diags, [](const Diagnostic& D) {
            return D.Severity == DiagSeverity::Error; }))
        return;
    if (!flowIsFollowable(Block)) return;

    // Anything a nested procedure names might be assigned in there, out of
    // sight of this walk.
    std::set<std::string> Hidden;
    collectNamesUsedIn(Block.Procs, Hidden);

    auto SavedTracked  = std::move(FlowTracked_);
    auto SavedResults  = std::move(FlowResultNames_);
    auto SavedReported = std::move(FlowReported_);
    FlowTracked_.clear();
    FlowResultNames_.clear();
    FlowReported_.clear();

    for (const auto& Vg : Block.Vars) {
        for (const auto& Nm : Vg.Names) {
            const std::string Key = toLower(Nm);
            if (Hidden.contains(Key)) continue;
            const Symbol* Sym = Symtab.lookup(Nm);
            // A variable bound to something outside the program (EP §6.4.1)
            // has a value from there, and one the program never took the
            // address of is the only kind the walk can speak for.
            if (!Sym || Sym->Kind != SymbolKind::Var) continue;
            if (Sym->IsBindable) continue;
            if (!isSimpleValue(Sym->Ty.get())) continue;
            FlowTracked_.insert(Key);
        }
    }

    // §6.6.2 applies to the body of a function, which is the block owned by
    // one.  The result answers to the function's own identifier, and in
    // Extended Pascal to the name the heading may have given it as well.
    const ProcDecl* Owner = CurrentProc;
    bool IsFunc = Owner && Owner->heading().IsFunction
               && Owner->Body.get() == &Block;
    if (IsFunc) {
        FlowResultNames_.insert(toLower(Owner->Name));
        if (!Owner->heading().ResultName.empty())
            FlowResultNames_.insert(toLower(Owner->heading().ResultName));
        // §6.8.2.2 lets a function nested inside this one assign the result,
        // and the walk does not go in there.  Whether every path assigns it is
        // then not a question this walk can answer; that there is an
        // assignment at all is still checked, on the frame, where it works.
        for (const auto& Nm : FlowResultNames_)
            if (Hidden.contains(Nm)) { IsFunc = false; break; }
        if (!IsFunc) FlowResultNames_.clear();
    }

    if (!FlowTracked_.empty() || IsFunc) {
        FlowState St;
        flowStmt(Block.Body.get(), St);

        // A function that assigns its result nowhere at all is already an
        // error, reported against the declaration; saying it again here as a
        // warning would be two messages for one mistake.
        if (IsFunc && !St.ResultAssigned && !FuncStack.empty()
            && FuncStack.back().HasResult)
            warning(Owner->Loc, diag::warn_result_not_always_set, {Owner->Name});
    }

    FlowTracked_     = std::move(SavedTracked);
    FlowResultNames_ = std::move(SavedResults);
    FlowReported_    = std::move(SavedReported);
}
