// StackHeadroom.cpp — see include/plang/Basic/StackHeadroom.h for the design
// rationale (issue #556, generalizing Parser::powerStackNearlyExhausted's own
// proven design from issue #550/#551, PR #555).

#include "plang/Basic/StackHeadroom.h"

#include "llvm/Support/ProgramStack.h"

#include <algorithm>

namespace plang {

std::uintptr_t captureStackBaseline() {
    return llvm::getStackPointer();
}

bool stackNearlyExhausted(std::uintptr_t Baseline,
                           std::size_t SafetyMarginCeiling) {
    const std::uintptr_t Current = llvm::getStackPointer();
    // The stack grows down on every architecture plang targets (x86-64,
    // AArch64), and Baseline was captured at the owning object's own
    // construction, further up an always-shallower stack than any point the
    // guarded recursion itself can reach, so Baseline >= Current holds once
    // any recursion at all has happened; the clamp below is just defensive
    // in case some unusual environment violates that (e.g. a split/segmented
    // stack where the two addresses are not directly comparable this way).
    const std::uintptr_t Used = (Baseline > Current) ? (Baseline - Current) : 0;
    const std::size_t Budget = llvm::getDefaultStackSize();
    // Scale the margin down for a small budget rather than treating
    // SafetyMarginCeiling as an absolute floor: a budget at or below that
    // fixed ceiling (a small-but-real platform limit -- see
    // DefaultStackSafetyMarginCeiling's own comment for concrete examples)
    // must not make every call here look "exhausted" independent of how much
    // of the budget is actually in use, which is what comparing against an
    // unscaled, possibly-larger-than-the-whole-budget margin did (a trivial,
    // zero-nesting expression was rejected outright under such a budget --
    // found in PR #555's own review). Reserving a quarter of the real
    // budget, capped at the original ceiling, keeps a normal-sized
    // (multi-MiB) stack's behavior byte-for-byte identical to before while
    // still reserving genuine headroom -- comfortably more than one frame of
    // the guarded recursion ever costs -- to safely unwind and tear down
    // whatever state was already built once a small budget's own,
    // correspondingly smaller ceiling is reached, so genuinely deep input is
    // still caught before it actually overflows the stack rather than the
    // check being disabled outright.
    const std::size_t Margin = std::min(SafetyMarginCeiling, Budget / 4);
    return Used >= (Budget - Margin);
}

} // namespace plang
