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
// Complete as written: of the thirteen statement kinds, the nine that
// contain a statement appear in walkStmts and the ten that hang an
// expression off themselves appear in forEachStmtExpr; of the eighteen
// expression kinds, six are leaves and the other twelve appear in
// walkExprs.
static_assert(NumStmtKinds == 13,
              "a new statement kind that contains statements needs a branch in "
              "walkStmts, and one that hangs an expression off itself needs a "
              "branch in forEachStmtExpr");
static_assert(NumExprKinds == 18,
              "a new expression kind that owns a child expression needs a "
              "branch in walkExprs");

/// Pre-order walk of all statement nodes in the tree rooted at S.
/// Calls F(S) first, then recurses into all sub-statements.
/// Does not cross procedure/function body boundaries.
template<std::invocable<const StmtNode*> Fn>
void walkStmts(const StmtNode* S, Fn&& F) {
    if (!S) return;
    F(S);
    if (auto* Cs = llvm::dyn_cast<CompoundStmt>(S)) {
        for (const auto& St : Cs->Stmts) walkStmts(St.get(), F);
    } else if (auto* Is = llvm::dyn_cast<IfStmt>(S)) {
        walkStmts(Is->Then.get(), F);
        walkStmts(Is->Else.get(), F);
    } else if (auto* Fs = llvm::dyn_cast<ForStmt>(S)) {
        walkStmts(Fs->Body.get(), F);
    } else if (auto* Fi = llvm::dyn_cast<ForInStmt>(S)) {
        walkStmts(Fi->Body.get(), F);
    } else if (auto* Ws = llvm::dyn_cast<WhileStmt>(S)) {
        walkStmts(Ws->Body.get(), F);
    } else if (auto* Rs = llvm::dyn_cast<RepeatStmt>(S)) {
        for (const auto& St : Rs->Stmts) walkStmts(St.get(), F);
    } else if (auto* Cas = llvm::dyn_cast<CaseStmt>(S)) {
        for (const auto& Arm : Cas->Arms) walkStmts(Arm.Body.get(), F);
        walkStmts(Cas->Else.get(), F);
    } else if (auto* Wts = llvm::dyn_cast<WithStmt>(S)) {
        walkStmts(Wts->Body.get(), F);
    } else if (auto* Ls = llvm::dyn_cast<LabeledStmt>(S)) {
        walkStmts(Ls->Stmt.get(), F);
    }
}

/// Pre-order walk of every expression node in the tree rooted at E.
/// Calls F(E) first, then recurses into the sub-expressions.
///
/// See NumExprKinds in AstBase.h: every kind that owns a child expression has
/// to appear below, or the walk silently stops short of it.
template<std::invocable<const ExprNode*> Fn>
void walkExprs(const ExprNode* E, Fn&& F) {
    if (!E) return;
    F(E);
    if (auto* N = llvm::dyn_cast<IndexExpr>(E)) {
        walkExprs(N->Array.get(), F);
        walkExprs(N->Index.get(), F);
    } else if (auto* N = llvm::dyn_cast<FieldExpr>(E)) {
        walkExprs(N->Record.get(), F);
    } else if (auto* N = llvm::dyn_cast<DerefExpr>(E)) {
        walkExprs(N->Pointer.get(), F);
    } else if (auto* N = llvm::dyn_cast<BinaryExpr>(E)) {
        walkExprs(N->Left.get(), F);
        walkExprs(N->Right.get(), F);
    } else if (auto* N = llvm::dyn_cast<UnaryExpr>(E)) {
        walkExprs(N->Operand.get(), F);
    } else if (auto* N = llvm::dyn_cast<CallExpr>(E)) {
        for (const auto& A : N->Args) walkExprs(A.get(), F);
    } else if (auto* N = llvm::dyn_cast<MethodCallExpr>(E)) {
        walkExprs(N->Receiver.get(), F);
        for (const auto& A : N->Args) walkExprs(A.get(), F);
    } else if (auto* N = llvm::dyn_cast<SetRangeExpr>(E)) {
        walkExprs(N->Low.get(), F);
        walkExprs(N->High.get(), F);
    } else if (auto* N = llvm::dyn_cast<SetLiteralExpr>(E)) {
        for (const auto& X : N->Elements) walkExprs(X.get(), F);
    } else if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) {
        walkExprs(N->Str.get(), F);
        walkExprs(N->Low.get(), F);
        walkExprs(N->High.get(), F);
    } else if (auto* N = llvm::dyn_cast<StructuredValueExpr>(E)) {
        for (const auto& Arm : N->Arms) {
            for (const auto& L : Arm.Labels) walkExprs(L.get(), F);
            walkExprs(Arm.Value.get(), F);
        }
    } else if (auto* N = llvm::dyn_cast<WriteParam>(E)) {
        walkExprs(N->Value.get(), F);
        walkExprs(N->Width.get(), F);
        walkExprs(N->Decimals.get(), F);
    } else if (auto* N = llvm::dyn_cast<TypeCastExpr>(E)) {
        walkExprs(N->Operand.get(), F);
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
