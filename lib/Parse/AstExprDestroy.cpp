//===- AstExprDestroy.cpp - Iterative teardown for chain-shaped Expr nodes ==//
//
// See the big comment on IndexExpr (AstExpr.h, immediately above its own
// declaration) for the full story of why IndexExpr/FieldExpr/DerefExpr/
// BinaryExpr/SubstringExpr/MethodCallExpr each declare a hand-written
// destructor instead of getting the compiler-generated (implicitly
// recursive) one every other AST node relies on. Short version: each of
// these six can be built into an arbitrarily deep, flat chain by an
// iterative parser loop (Parser::parsePostfix for the first five;
// parseSimpleExpr/parseTerm's addop/mulop folding loops for BinaryExpr's
// Left, parsePower's own right recursion for BinaryExpr's Right) with no
// depth limit tied to the AST's own shape, and destroying a chain that deep
// through the ordinary compiler-generated destructor recurses the real C++
// call stack one frame per link -- issue #551.
//
// This file is the destructor half of that fix. The other half --
// preventing parsePower's own C++ call recursion from itself overflowing
// the stack while BUILDING an adversarially long '**' chain, before this
// file's code ever gets a chance to run -- is issue #550, fixed separately
// in ParseExpr.cpp/Parser.h (parsePower, powerStackNearlyExhausted,
// Parser::StackBaseline). The two are independent: this file's destructors
// make ANY sufficiently deep chain of these six kinds safe to tear down
// however it got built, so a future gap in some OTHER parser guard cannot
// reopen this specific crash the way the reverted PR #553 left parsePostfix
// exposed after only patching parseSimpleExpr/parseTerm.
//
// Lives in lib/Parse (PlangParse), not lib/AST (PlangAST), even though it is
// AST node code: PlangAST is specifically the AstPrinter.cpp dump/debug-
// output library, deliberately kept out of every non-dump consumer's link
// (see fuzz/CMakeLists.txt's own comment -- PlangASTFuzz is linked into
// none of the three fuzzer executables for exactly that reason). This file
// is the opposite of dump-only: it defines these six types' real
// destructors, so it must be linked into EVERY binary that ever constructs
// one -- in practice, anything that links PlangParse, which is already the
// lowest-level target common to the compiler proper, every fuzz target that
// touches parsing (PlangParseFuzz, PlangSemaFuzz), and any unit test that
// links PlangParse directly. Declaring these six destructors instead of
// leaving them implicitly defaulted, without also putting the matching
// definitions somewhere every one of those already depends on, would trade
// a stack-overflow bug for a link error.
//
//===----------------------------------------------------------------------===//

#include "plang/AST/Ast.h"
#include "llvm/Support/Casting.h"

#include <utility>
#include <vector>

using namespace plang;

namespace {

// Moves N's own "chain" child (or children, for BinaryExpr, which can chain
// through either operand) onto WorkList, leaving N holding null in its
// place. Every other field these six types carry -- IndexExpr::Index,
// SubstringExpr::Low/High, MethodCallExpr::Args, BinaryExpr::Op -- is left
// untouched: those are ordinary, independently-bounded sub-expressions (or
// not an owned child at all), not the field a long chain of THIS construct
// threads through, and are safe to let destruct normally when N itself
// does, whether that is a leaf, one of these same six kinds (whose own
// destructor does this same detach-then-destruct step for its own chain
// field), or a subtree Parser::ExprDepth already keeps under 500 levels.
void collectChainChildren(ExprNode* N, std::vector<std::unique_ptr<ExprNode>>& WorkList) {
    switch (N->Kind) {
        case NodeKind::IndexExpr: {
            auto* I = llvm::cast<IndexExpr>(N);
            if (I->Array) WorkList.push_back(std::move(I->Array));
            break;
        }
        case NodeKind::FieldExpr: {
            auto* F = llvm::cast<FieldExpr>(N);
            if (F->Record) WorkList.push_back(std::move(F->Record));
            break;
        }
        case NodeKind::DerefExpr: {
            auto* D = llvm::cast<DerefExpr>(N);
            if (D->Pointer) WorkList.push_back(std::move(D->Pointer));
            break;
        }
        case NodeKind::BinaryExpr: {
            auto* B = llvm::cast<BinaryExpr>(N);
            if (B->Left)  WorkList.push_back(std::move(B->Left));
            if (B->Right) WorkList.push_back(std::move(B->Right));
            break;
        }
        case NodeKind::SubstringExpr: {
            auto* S = llvm::cast<SubstringExpr>(N);
            if (S->Str) WorkList.push_back(std::move(S->Str));
            break;
        }
        case NodeKind::MethodCallExpr: {
            auto* M = llvm::cast<MethodCallExpr>(N);
            if (M->Receiver) WorkList.push_back(std::move(M->Receiver));
            break;
        }
        default:
            // Not one of the six chain-shaped kinds: nothing to detach here,
            // N's own (already-safe) destructor handles the rest of it.
            break;
    }
}

// Non-recursively destroys the subtree rooted at Root, which may chain
// arbitrarily deep through any mixture of the six kinds
// collectChainChildren knows about -- a postfix chain can freely mix
// 'x[1].f^.g[2]...', and a BinaryExpr chain can have a chain-shaped node as
// either operand. An explicit std::vector stands in for the C++ call stack
// a naive recursive walk would spend one frame per link on.
void drainExprChain(std::unique_ptr<ExprNode> Root) {
    if (!Root) return;
    std::vector<std::unique_ptr<ExprNode>> Stack;
    Stack.push_back(std::move(Root));
    while (!Stack.empty()) {
        std::unique_ptr<ExprNode> N = std::move(Stack.back());
        Stack.pop_back();
        if (!N) continue;
        // Detach N's own chain child/children BEFORE N is allowed to
        // destruct at the end of this iteration: by the time N's
        // destructor actually runs, those fields are already null, so even
        // though N's own hand-written destructor (if N is one of these six
        // kinds) calls drainExprChain on them too, that nested call sees
        // Root == nullptr and returns immediately instead of recursing this
        // same walk one level deeper -- which is exactly what keeps this
        // whole process to one stack frame regardless of chain length.
        collectChainChildren(N.get(), Stack);
    } // N (and whatever non-chain fields it still owns) destructs here,
      // each iteration, at ordinary, bounded cost.
}

} // namespace

IndexExpr::~IndexExpr() { drainExprChain(std::move(Array)); }
FieldExpr::~FieldExpr() { drainExprChain(std::move(Record)); }
DerefExpr::~DerefExpr() { drainExprChain(std::move(Pointer)); }
BinaryExpr::~BinaryExpr() {
    drainExprChain(std::move(Left));
    drainExprChain(std::move(Right));
}
SubstringExpr::~SubstringExpr() { drainExprChain(std::move(Str)); }
MethodCallExpr::~MethodCallExpr() { drainExprChain(std::move(Receiver)); }
