#pragma once

// Internal header shared by Sema*.cpp translation units.
// Not intended for inclusion outside the Sema implementation.

#include "plang/AST/Ast.h"
#include "llvm/Support/Casting.h"

#include <concepts>

namespace plang {

// These two walks are dyn_cast chains, not switches, so nothing about them is
// exhaustive and a kind left out is not a missing case but a subtree the walk
// never enters.  That is worse here than it would be in a printer: the
// §6.8.3.9 for-loop analysis and the definite-assignment walk both ride on
// these, so a statement kind missing from walkStmts does not lose a line of
// output, it silently stops reporting a class of error inside it.
//
// See NumExprKinds and NumStmtKinds in AstBase.h.
// Complete as written: of the fourteen statement kinds, the nine that
// contain a statement appear in walkStmts and the eleven that hang an
// expression off themselves appear in forEachStmtExpr; of the nineteen
// expression kinds, six are leaves and the other thirteen appear in
// walkExprs.
static_assert(NumStmtKinds == 14,
              "a new statement kind that contains statements needs a branch in "
              "walkStmts, and one that hangs an expression off itself needs a "
              "branch in forEachStmtExpr");
static_assert(NumExprKinds == 19,
              "a new expression kind that owns a child expression needs a "
              "branch in walkExprs");

// Ceiling on live walkStmts/walkExprs recursion activations, so a
// pathologically deep tree -- a flat `1+1+1+...+1` chain built specifically
// to exhaust the stack, or generated/fuzzed input -- cannot walk this deep
// either.  Checked before Depth is bumped, so a caller already sitting at the
// ceiling returns without recursing once more.  1000, the same budget as
// Sema::ExprDepth/MaxExprDepth (Sema.h), for the same reason: comfortably
// under the empirical crash threshold on an 8MB default stack, while no
// legitimate Pascal program -- handwritten or reasonably generated -- nests
// anywhere close this deep.  If anything this is MORE conservative here than
// it is for checkExpr: a walkExprs/walkStmts activation is a couple of
// dyn_casts and a call, far lighter than a checkExpr activation's full type
// resolution, so the real stack-exhaustion threshold for these two is well
// past checkExpr's own.
//
// Declining silently once the ceiling is reached -- no diagnostic of its
// own -- mirrors constBound/buildExtentForm (SemaType.cpp, issue #204)
// rather than checkExpr itself: every current walkStmts/walkExprs call site
// (see the grep below) runs on a tree that Sema's own checkStmt/checkExpr
// has either already fully walked, or is about to walk unconditionally
// moments later in the same caller -- so checkExpr's existing "expression is
// nested too deeply" (or the parser's own statement-nesting guard) is always
// the one that actually reports a too-deep tree; this ceiling exists only so
// walkStmts/walkExprs' OWN recursion cannot be the thing that exhausts the
// stack first while getting there.  A second, independent diagnostic here
// would just be checkExpr's own error repeated: checkBlock's refsPendingEnum
// (Sema.cpp) tests a const initializer for a forward enum reference and
// falls straight through to defineConst's checkExpr call (immediately, or
// later via StructuredConsts) whether or not it found one; checkForBody's
// var-reuse scan (SemaStmt.cpp) is followed unconditionally by checkStmt on
// the very same body; every walkStmts/walkExprs call reachable from
// SemaFlow.cpp's checkDefiniteAssignment runs after Phase 6's checkStmt has
// already fully checked the same Block.Body, and checkDefiniteAssignment
// bails out immediately if Diags already holds an error; recordModifiedParams
// and the Phase 6.5 for-loop-threat scan (Sema.cpp) both run after checkBlock
// has already returned; and CodeGen's one call site (LabelGotoEngine.cpp) is
// unreachable on a too-deep tree at all, since Frontend.cpp never constructs
// a Codegen unless Sema::check already returned success.
//
// grep for every current external call site: `walkExprs(\|walkStmts(` under
// lib/ excluding this file's own recursive self-calls below.  Adding a NEW
// one that does NOT have this property -- one that can be first to touch a
// tree checkExpr/checkStmt has not yet validated and is not about to,
// unconditionally, moments later in the same caller -- needs its own
// ceiling-and-diagnostic the way checkExpr has one, not silent reliance on
// this one.
constexpr unsigned MaxWalkDepth = 1000;

/// Pre-order walk of all statement nodes in the tree rooted at S.
/// Calls F(S) first, then recurses into all sub-statements.
/// Does not cross procedure/function body boundaries.
///
/// \p Depth is an implementation detail (recursion depth so far), not a
/// parameter callers pass; see MaxWalkDepth above.
template<std::invocable<const StmtNode*> Fn>
void walkStmts(const StmtNode* S, Fn&& F, unsigned Depth = 0) {
    if (!S) return;
    if (Depth >= MaxWalkDepth) return;
    F(S);
    if (auto* Cs = llvm::dyn_cast<CompoundStmt>(S)) {
        for (const auto& St : Cs->Stmts) walkStmts(St.get(), F, Depth + 1);
    } else if (auto* Is = llvm::dyn_cast<IfStmt>(S)) {
        walkStmts(Is->Then.get(), F, Depth + 1);
        walkStmts(Is->Else.get(), F, Depth + 1);
    } else if (auto* Fs = llvm::dyn_cast<ForStmt>(S)) {
        walkStmts(Fs->Body.get(), F, Depth + 1);
    } else if (auto* Fi = llvm::dyn_cast<ForInStmt>(S)) {
        walkStmts(Fi->Body.get(), F, Depth + 1);
    } else if (auto* Ws = llvm::dyn_cast<WhileStmt>(S)) {
        walkStmts(Ws->Body.get(), F, Depth + 1);
    } else if (auto* Rs = llvm::dyn_cast<RepeatStmt>(S)) {
        for (const auto& St : Rs->Stmts) walkStmts(St.get(), F, Depth + 1);
    } else if (auto* Cas = llvm::dyn_cast<CaseStmt>(S)) {
        for (const auto& Arm : Cas->Arms) walkStmts(Arm.Body.get(), F, Depth + 1);
        walkStmts(Cas->Else.get(), F, Depth + 1);
    } else if (auto* Wts = llvm::dyn_cast<WithStmt>(S)) {
        walkStmts(Wts->Body.get(), F, Depth + 1);
    } else if (auto* Ls = llvm::dyn_cast<LabeledStmt>(S)) {
        walkStmts(Ls->Stmt.get(), F, Depth + 1);
    }
}

/// Pre-order walk of every expression node in the tree rooted at E.
/// Calls F(E) first, then recurses into the sub-expressions.
///
/// See NumExprKinds in AstBase.h: every kind that owns a child expression has
/// to appear below, or the walk silently stops short of it.
///
/// \p Depth is an implementation detail (recursion depth so far), not a
/// parameter callers pass; see MaxWalkDepth above.
template<std::invocable<const ExprNode*> Fn>
void walkExprs(const ExprNode* E, Fn&& F, unsigned Depth = 0) {
    if (!E) return;
    if (Depth >= MaxWalkDepth) return;
    F(E);
    if (auto* N = llvm::dyn_cast<IndexExpr>(E)) {
        walkExprs(N->Array.get(), F, Depth + 1);
        walkExprs(N->Index.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<FieldExpr>(E)) {
        walkExprs(N->Record.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<DerefExpr>(E)) {
        walkExprs(N->Pointer.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<BinaryExpr>(E)) {
        walkExprs(N->Left.get(), F, Depth + 1);
        walkExprs(N->Right.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<UnaryExpr>(E)) {
        walkExprs(N->Operand.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<CallExpr>(E)) {
        for (const auto& A : N->Args) walkExprs(A.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<MethodCallExpr>(E)) {
        walkExprs(N->Receiver.get(), F, Depth + 1);
        for (const auto& A : N->Args) walkExprs(A.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<InheritedCallExpr>(E)) {
        // No Receiver to walk -- see InheritedCallExpr's own comment
        // (AstExpr.h): the receiver is always the implicit Self.
        for (const auto& A : N->Args) walkExprs(A.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SetRangeExpr>(E)) {
        walkExprs(N->Low.get(), F, Depth + 1);
        walkExprs(N->High.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SetLiteralExpr>(E)) {
        for (const auto& X : N->Elements) walkExprs(X.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) {
        walkExprs(N->Str.get(), F, Depth + 1);
        walkExprs(N->Low.get(), F, Depth + 1);
        walkExprs(N->High.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<StructuredValueExpr>(E)) {
        for (const auto& Arm : N->Arms) {
            for (const auto& L : Arm.Labels) walkExprs(L.get(), F, Depth + 1);
            walkExprs(Arm.Value.get(), F, Depth + 1);
        }
    } else if (auto* N = llvm::dyn_cast<WriteParam>(E)) {
        walkExprs(N->Value.get(), F, Depth + 1);
        walkExprs(N->Width.get(), F, Depth + 1);
        walkExprs(N->Decimals.get(), F, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<TypeCastExpr>(E)) {
        walkExprs(N->Operand.get(), F, Depth + 1);
    }
}

/// Calls F(E) for every expression that hangs off this one statement, without
/// descending into the statements it contains.  Pair it with walkStmts to
/// reach every expression in a statement tree.
template<std::invocable<const ExprNode*> Fn>
void forEachStmtExpr(const StmtNode* S, Fn&& F) {
    if (!S) return;
    if (auto* N = llvm::dyn_cast<AssignStmt>(S)) {
        F(N->Target.get());
        F(N->Value.get());
    } else if (auto* N = llvm::dyn_cast<CallStmt>(S)) {
        for (const auto& A : N->Args) F(A.get());
    } else if (auto* N = llvm::dyn_cast<MethodCallStmt>(S)) {
        F(N->Receiver.get());
        for (const auto& A : N->Args) F(A.get());
    } else if (auto* N = llvm::dyn_cast<InheritedCallStmt>(S)) {
        for (const auto& A : N->Args) F(A.get());
    } else if (auto* N = llvm::dyn_cast<IfStmt>(S)) {
        F(N->Cond.get());
    } else if (auto* N = llvm::dyn_cast<WhileStmt>(S)) {
        F(N->Cond.get());
    } else if (auto* N = llvm::dyn_cast<RepeatStmt>(S)) {
        F(N->Cond.get());
    } else if (auto* N = llvm::dyn_cast<ForStmt>(S)) {
        F(N->From.get());
        F(N->Limit.get());
    } else if (auto* N = llvm::dyn_cast<ForInStmt>(S)) {
        F(N->SetExpr.get());
    } else if (auto* N = llvm::dyn_cast<CaseStmt>(S)) {
        F(N->Selector.get());
        for (const auto& Arm : N->Arms)
            for (const auto& L : Arm.Labels) {
                F(L.Low.get());
                F(L.High.get());
            }
    } else if (auto* N = llvm::dyn_cast<WithStmt>(S)) {
        for (const auto& R : N->Records) F(R.get());
    }
}

} // namespace plang
