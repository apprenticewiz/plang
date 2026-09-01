// StackHeadroom.h — shared stack-headroom-check utility.
//
// Several independent, mutually-recursive AST walks in this codebase
// (Parser::parsePower's own '**'/'pow' chain recursion, Sema::checkExpr/
// checkBinary and SemaType.cpp's constBound/buildExtentForm, and CodeGen's
// CGBinaryOps::emitBinary <-> CGExprCore::emitExpr) all recurse deeply enough
// on adversarial input to risk exhausting the real C++ call stack rather than
// failing with a clean diagnostic (issue #556, the direct follow-up to
// issue #550/#551's identical problem in the parser alone, PR #555).
//
// All three subsystems share the same genuine resource (the C++ call stack)
// and the same detection mechanism (live stack-pointer headroom against the
// platform's real stack budget), so this is one shared utility rather than
// three independent copies of the same math -- see issue #556's own comment,
// and issue #300's original "one budget mechanism, not N ad hoc guards"
// question this generalizes further. This is deliberately NOT a term-count
// ceiling the way MaxExprDepth/ExprDepth (Parser.h, Sema.h) and their
// siblings are: the "right" term count is not a fixed number, it depends on
// how much real stack a single frame of the recursion in question costs,
// which varies by call site (a parsePower frame is cheap; a CGExprCore::
// emitExpr frame reached through several layers of std::function/lambda glue
// is not), by build type (Debug frames are substantially larger than
// Release's optimized ones), and by platform -- none of which a compile-time
// constant can track but a live headroom check naturally does. See
// stackNearlyExhausted's own comment for the full design rationale, first
// worked out (and twice adversarially corrected) for Parser::parsePower's
// own guard (lib/Parse/ParseExpr.cpp, powerStackNearlyExhausted, now migrated
// to call this instead of keeping its own independent copy).
#pragma once

#include <cstddef>
#include <cstdint>

namespace plang {

/// Ceiling on the safety margin subtracted from the platform's real stack
/// budget before stackNearlyExhausted (below) treats itself as "nearly
/// exhausted". Covers (a) whatever of the thread's stack was already spent
/// between process start and a call site's own Baseline being captured, and
/// (b) headroom for everything that still has to run on this same stack once
/// the recursion in question stops: unwinding back out through every caller
/// still on the stack, diagnostic emission, and (for the parser specifically,
/// issue #551's own concern) AST teardown of whatever was already built.
/// 1 MiB is generous for all of that against an 8MiB default stack while
/// still leaving the overwhelming majority of the budget available to
/// recurse into.
///
/// This is a *ceiling*, not the margin actually used: stackNearlyExhausted
/// scales it down for unusually small budgets (see that function's own
/// comment) so that a small-but-real platform stack limit -- a constrained
/// container, a hardened deployment, or a fuzzer worker's stack, any of which
/// can be at or below this many bytes -- cannot make the check treat itself
/// as "already exhausted" independent of how much of the budget is actually
/// in use (found in PR #555's own review, issue #550's own follow-up).
inline constexpr std::size_t DefaultStackSafetyMarginCeiling = 1u << 20; // 1 MiB

/// Returns the current thread's stack pointer, for a call site to capture as
/// its own Baseline (below) at construction of whatever object's lifetime
/// spans every recursive call the headroom check below needs to cover --
/// e.g. Parser::StackBaseline captured at Parser construction, Sema's own
/// analogous member captured at Sema construction, CGExprCore's own analogous
/// member captured at CGExprCore construction. Captured once, at
/// construction, rather than lazily on the guarded function's first call, so
/// it reflects the shallowest point that object is ever active at, regardless
/// of how much unrelated recursion (e.g. parenthesized nesting, bounded
/// separately by its own term-count ceiling) has already gone by the time the
/// guarded recursive edge is first reached.
std::uintptr_t captureStackBaseline();

/// True once continuing to recurse on the calling thread would risk running
/// the real C++ call stack past the platform's own limit, measured from
/// \p Baseline (see captureStackBaseline above).
///
/// Unlike a term-count ceiling (MaxExprDepth/ExprDepth and siblings
/// elsewhere in this codebase), which bounds a *count* of live activations
/// against a fixed constant, this is bounded by comparing actual stack-
/// pointer headroom against the platform's real stack budget -- the same way
/// clang::Sema::isStackNearlyExhausted() does (clang/Basic/Stack.h, not
/// linked into plang but a design precedent): llvm::getStackPointer() and
/// llvm::getDefaultStackSize() (llvm/Support/ProgramStack.h) are both already
/// reusable utilities LLVM ships and this project already links against
/// (confirmed: getDefaultStackSize() is backed by getrlimit(RLIMIT_STACK) on
/// POSIX, with its own platform-appropriate fallback when that is
/// unavailable or unlimited, so this tracks the actual runtime stack budget
/// rather than guessing at one).
///
/// \param Baseline  The stack pointer captured at some shallower, fixed
///                  reference point (captureStackBaseline, above) that usage
///                  is measured relative to.
/// \param SafetyMarginCeiling  Ceiling on the safety margin subtracted from
///                  the platform's actual stack budget before this treats
///                  itself as "nearly exhausted"; see
///                  DefaultStackSafetyMarginCeiling's own comment for why the
///                  exact number here is far less load-bearing than it would
///                  be for a term-count ceiling: get it somewhat wrong in
///                  either direction and the worst case is rejecting a chain
///                  a few thousand terms earlier or later than strictly
///                  necessary, not silently narrowing accepted syntax.
bool stackNearlyExhausted(
    std::uintptr_t Baseline,
    std::size_t SafetyMarginCeiling = DefaultStackSafetyMarginCeiling);

/// RAII bump/unbump of a live-activation counter across one activation of a
/// guarded recursive function, resetting an associated "already reported"
/// latch flag once the counter returns to zero -- the same shape
/// ExprDepthScope/TypeDepthScope/... (Parser.h, Sema.h) already use for a
/// pure term-count ceiling, generalized here for reuse by a stackNearlyExhausted-based guard specifically.
///
/// A pure term-count ceiling (ExprDepthScope and siblings) can only ever fire
/// once N-1 other guards of the same kind are already alive on the stack, so
/// a Guard is always already there, from some earlier activation, to
/// eventually reset the "already reported" flag as the stack unwinds. A
/// stackNearlyExhausted-based check is different: because it is driven by
/// absolute stack headroom rather than a count, it can fire on the very
/// *first* call, before any Guard for this counter has ever been
/// constructed. So every call site using this guard alongside
/// stackNearlyExhausted MUST construct it unconditionally, before running
/// the exhaustion check, rather than only in the "not exhausted, about to
/// recurse further" branch -- otherwise a first-call exhaustion would latch
/// the "already reported" flag true with no Guard ever created to reset it,
/// silently poisoning every later, unrelated recursive entry point sharing
/// the same counter for the rest of the flag's owning object's lifetime
/// (found, for Parser::PowerDepthScope, in PR #555's own review; see
/// Parser::parsePower's call site, ParseExpr.cpp, for a worked example of
/// this ordering).
struct RecursionGuard {
    unsigned& N;
    bool&     LimitHit;
    explicit RecursionGuard(unsigned& Counter, bool& LimitHitFlag)
        : N(Counter), LimitHit(LimitHitFlag) { ++N; }
    ~RecursionGuard() { if (--N == 0) LimitHit = false; }
};

} // namespace plang
