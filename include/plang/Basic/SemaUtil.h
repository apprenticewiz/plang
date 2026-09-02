#pragma once

// Internal header shared by Sema*.cpp translation units.
// Not intended for inclusion outside the Sema implementation.

#include "plang/AST/Ast.h"
#include "plang/Basic/StackHeadroom.h"
#include "llvm/Support/Casting.h"

#include <concepts>
#include <cstdint>

namespace plang {

// These two walks are dyn_cast chains, not switches, so nothing about them is
// exhaustive and a kind left out is not a missing case but a subtree the walk
// never enters.  That is worse here than it would be in a printer: the
// §6.8.3.9 for-loop analysis and the definite-assignment walk both ride on
// these, so a statement kind missing from walkStmts does not lose a line of
// output, it silently stops reporting a class of error inside it.
//
// See NumExprKinds and NumStmtKinds in AstBase.h.
// Complete as written: of the fifteen statement kinds, the nine that
// contain a statement appear in walkStmts and the twelve that hang an
// expression off themselves appear in forEachStmtExpr; of the twenty
// expression kinds, six are leaves and the other fourteen appear in
// walkExprs.
static_assert(NumStmtKinds == 15,
              "a new statement kind that contains statements needs a branch in "
              "walkStmts, and one that hangs an expression off itself needs a "
              "branch in forEachStmtExpr");
static_assert(NumExprKinds == 20,
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

// Issue #556's follow-up (found in that issue's own adversarial review,
// PR #558): `const x = 2**2**...**2;` (a flat chain hundreds of terms deep,
// entirely ordinary syntax -- no nesting, no fuzzing needed) reaches
// Sema::checkBlock's refsPendingEnum lambda, which walks the SAME kind of
// flat-chain AST as checkExpr/constBound/buildExtentForm do, through THIS
// walk instead -- and MaxWalkDepth alone has exactly the gap
// checkExpr/constBound/buildExtentForm already had before issue #556: it
// bounds a term COUNT, not the real C++ stack, so a small-but-real platform
// stack budget (a constrained container, a hardened deployment, a fuzzer
// worker) can exhaust the actual stack before 1000 live walkStmts/walkExprs
// activations ever accumulate -- confirmed via gdb, `ulimit -s 256`, Debug:
// a 700-term flat `**` chain crashes inside walkExprs's own recursion,
// nowhere near MaxWalkDepth.
//
// So every walk below also checks plang::stackNearlyExhausted (Basic/
// StackHeadroom.h), exactly the way checkExpr/constBound/buildExtentForm
// do, measured from a \p Baseline the CALLER supplies rather than one this
// header captures for itself: capturing a fresh baseline on every top-level
// walkStmts/walkExprs call, the way a lazily-captured "first call" baseline
// would, is wrong here specifically BECAUSE walkStmts/walkExprs are free
// functions with call sites that nest inside one another (a walkStmts
// callback that itself calls forEachStmtExpr then walkExprs -- see
// Sema::recordModifiedParams and SemaFlow.cpp's collectNamesUsedIn) and
// inside a caller's own unrelated recursion elsewhere on the same stack; a
// baseline captured freshly at the INNER call's own entry would measure
// headroom from that already-deep point rather than from the true
// shallowest point of the whole call chain, undercounting real stack usage
// by however deep the outer recursion already was -- the identical bug this
// whole mechanism exists to catch, reintroduced one level up. Every Sema-
// side caller instead threads down the ONE Sema::StackBaseline captured
// once at Sema construction (Sema.h) -- valid from any depth this Sema
// instance is ever called at, nested or not, for the same reason it is
// already valid for checkExpr/constBound/buildExtentForm. LabelGotoEngine's
// call site (CodeGen/LabelGotoEngine.cpp) has no equivalent fixed reference
// to thread down, so it captures its own baseline once, at nonLocalTargets'
// own entry -- the shallowest point that particular call tree is known to
// start from.
//
// No diagnostic of its own here either, for exactly the reasons the comment
// above (MaxWalkDepth's own) already gives for the term-count ceiling: this
// second check catches the identical trees, by the identical "checkExpr/
// checkStmt already has, or is about to run, its own guarded walk over the
// same tree" property, just before the real stack rather than the term
// count runs out first.
//
// Unlike Sema::ExprDepthScope (a RecursionGuard bumping a MEMBER counter,
// needed there because checkExpr's many mutually-recursive helpers cannot
// all thread a Depth parameter through each other), walkStmts/walkExprs
// already thread Depth as an ordinary by-value parameter through their own
// direct self-recursion, so a live-activation count (in lockstep with the
// real call stack) comes for free with no separate RAII object needed: it
// is bumped by every recursive call and unwound automatically as the C++
// stack itself unwinds, with nothing left to reset on the way out.

/// Pre-order walk of all statement nodes in the tree rooted at S.
/// Calls F(S) first, then recurses into all sub-statements.
/// Does not cross procedure/function body boundaries.
///
/// \p Baseline is the stack-headroom reference point every call in this
/// walk is measured from (plang::captureStackBaseline(), or a caller's own
/// already-captured baseline -- see MaxWalkDepth's own comment above for why
/// this is a required parameter rather than something this function
/// captures for itself).
///
/// \p Depth is an implementation detail (recursion depth so far), not a
/// parameter callers pass; see MaxWalkDepth above.
template<std::invocable<const StmtNode*> Fn>
void walkStmts(const StmtNode* S, Fn&& F, std::uintptr_t Baseline, unsigned Depth = 0) {
    if (!S) return;
    if (Depth >= MaxWalkDepth || stackNearlyExhausted(Baseline)) return;
    F(S);
    if (auto* Cs = llvm::dyn_cast<CompoundStmt>(S)) {
        for (const auto& St : Cs->Stmts) walkStmts(St.get(), F, Baseline, Depth + 1);
    } else if (auto* Is = llvm::dyn_cast<IfStmt>(S)) {
        walkStmts(Is->Then.get(), F, Baseline, Depth + 1);
        walkStmts(Is->Else.get(), F, Baseline, Depth + 1);
    } else if (auto* Fs = llvm::dyn_cast<ForStmt>(S)) {
        walkStmts(Fs->Body.get(), F, Baseline, Depth + 1);
    } else if (auto* Fi = llvm::dyn_cast<ForInStmt>(S)) {
        walkStmts(Fi->Body.get(), F, Baseline, Depth + 1);
    } else if (auto* Ws = llvm::dyn_cast<WhileStmt>(S)) {
        walkStmts(Ws->Body.get(), F, Baseline, Depth + 1);
    } else if (auto* Rs = llvm::dyn_cast<RepeatStmt>(S)) {
        for (const auto& St : Rs->Stmts) walkStmts(St.get(), F, Baseline, Depth + 1);
    } else if (auto* Cas = llvm::dyn_cast<CaseStmt>(S)) {
        for (const auto& Arm : Cas->Arms) walkStmts(Arm.Body.get(), F, Baseline, Depth + 1);
        walkStmts(Cas->Else.get(), F, Baseline, Depth + 1);
    } else if (auto* Wts = llvm::dyn_cast<WithStmt>(S)) {
        walkStmts(Wts->Body.get(), F, Baseline, Depth + 1);
    } else if (auto* Ls = llvm::dyn_cast<LabeledStmt>(S)) {
        walkStmts(Ls->Stmt.get(), F, Baseline, Depth + 1);
    }
}

/// Pre-order walk of every expression node in the tree rooted at E.
/// Calls F(E) first, then recurses into the sub-expressions.
///
/// See NumExprKinds in AstBase.h: every kind that owns a child expression has
/// to appear below, or the walk silently stops short of it.
///
/// \p Baseline is the stack-headroom reference point every call in this
/// walk is measured from; see walkStmts' own comment (and MaxWalkDepth's,
/// above) for why this is a required parameter rather than something this
/// function captures for itself.
///
/// \p Depth is an implementation detail (recursion depth so far), not a
/// parameter callers pass; see MaxWalkDepth above.
template<std::invocable<const ExprNode*> Fn>
void walkExprs(const ExprNode* E, Fn&& F, std::uintptr_t Baseline, unsigned Depth = 0) {
    if (!E) return;
    if (Depth >= MaxWalkDepth || stackNearlyExhausted(Baseline)) return;
    F(E);
    if (auto* N = llvm::dyn_cast<IndexExpr>(E)) {
        walkExprs(N->Array.get(), F, Baseline, Depth + 1);
        walkExprs(N->Index.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<FieldExpr>(E)) {
        walkExprs(N->Record.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<DerefExpr>(E)) {
        walkExprs(N->Pointer.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<BinaryExpr>(E)) {
        walkExprs(N->Left.get(), F, Baseline, Depth + 1);
        walkExprs(N->Right.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<UnaryExpr>(E)) {
        walkExprs(N->Operand.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<CallExpr>(E)) {
        for (const auto& A : N->Args) walkExprs(A.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<MethodCallExpr>(E)) {
        walkExprs(N->Receiver.get(), F, Baseline, Depth + 1);
        for (const auto& A : N->Args) walkExprs(A.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<InheritedCallExpr>(E)) {
        // No Receiver to walk -- see InheritedCallExpr's own comment
        // (AstExpr.h): the receiver is always the implicit Self.
        for (const auto& A : N->Args) walkExprs(A.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<IndirectCallExpr>(E)) {
        walkExprs(N->Callee.get(), F, Baseline, Depth + 1);
        for (const auto& A : N->Args) walkExprs(A.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SetRangeExpr>(E)) {
        walkExprs(N->Low.get(), F, Baseline, Depth + 1);
        walkExprs(N->High.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SetLiteralExpr>(E)) {
        for (const auto& X : N->Elements) walkExprs(X.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<SubstringExpr>(E)) {
        walkExprs(N->Str.get(), F, Baseline, Depth + 1);
        walkExprs(N->Low.get(), F, Baseline, Depth + 1);
        walkExprs(N->High.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<StructuredValueExpr>(E)) {
        for (const auto& Arm : N->Arms) {
            for (const auto& L : Arm.Labels) walkExprs(L.get(), F, Baseline, Depth + 1);
            walkExprs(Arm.Value.get(), F, Baseline, Depth + 1);
        }
    } else if (auto* N = llvm::dyn_cast<WriteParam>(E)) {
        walkExprs(N->Value.get(), F, Baseline, Depth + 1);
        walkExprs(N->Width.get(), F, Baseline, Depth + 1);
        walkExprs(N->Decimals.get(), F, Baseline, Depth + 1);
    } else if (auto* N = llvm::dyn_cast<TypeCastExpr>(E)) {
        walkExprs(N->Operand.get(), F, Baseline, Depth + 1);
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
    } else if (auto* N = llvm::dyn_cast<IndirectCallStmt>(S)) {
        F(N->Callee.get());
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
