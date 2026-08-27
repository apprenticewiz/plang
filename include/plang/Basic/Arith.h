#pragma once

// Integer operations whose Pascal meaning differs from C's, shared by the
// constant folders and by codegen so that a folded constant and a value
// computed at run time never disagree.

#include <cstdint>
#include <optional>

namespace plang {

/// ISO §6.7.2.2 `i mod j`, whose result is always in [0, j).  C's `%` takes its
/// sign from the dividend instead, so (-17) % 5 is -2 where Pascal wants 3.
/// The caller is responsible for j > 0, which the standard also requires, and
/// (see divOverflows below) for this not being the one nonzero j that still
/// has no representable answer.
[[nodiscard]] constexpr int64_t isoMod(int64_t I, int64_t J) {
    const int64_t R = I % J;
    return R < 0 ? R + (J < 0 ? -J : J) : R;
}

/// Whether `i div j` / `i mod j` has no representable int64_t result even
/// though j is nonzero: -2^63 (minint) has no positive counterpart at +2^63,
/// so this one dividend/divisor pair overflows despite passing a zero-divisor
/// check.  Both C's `/` and `%` are signed-overflow UB for it, and on this
/// hardware that is a SIGFPE trap (x86 idiv faults on overflow the same way
/// it faults on a zero divisor) -- a constant folder must decline this pair
/// rather than compute it, the same reason RangeCheckGuards::
/// emitDivOverflowCheck exists for the identical pair at run time.
[[nodiscard]] constexpr bool divOverflows(int64_t I, int64_t J) {
    return J == -1 && I == INT64_MIN;
}

/// Checked 64-bit +, -, *: nullopt on signed overflow, rather than the UB a
/// plain `+`/`-`/`*` would be there (a sanitizer abort during development, a
/// silently wrapped value in a release build otherwise).  A constant folder
/// that overflows must decline the fold and hand nothing back, not the
/// wrapped value, as though it were the constant the source actually named.
[[nodiscard]] inline std::optional<int64_t> checkedAdd(int64_t L, int64_t R) {
    int64_t Result;
    if (__builtin_add_overflow(L, R, &Result)) return std::nullopt;
    return Result;
}
[[nodiscard]] inline std::optional<int64_t> checkedSub(int64_t L, int64_t R) {
    int64_t Result;
    if (__builtin_sub_overflow(L, R, &Result)) return std::nullopt;
    return Result;
}
[[nodiscard]] inline std::optional<int64_t> checkedMul(int64_t L, int64_t R) {
    int64_t Result;
    if (__builtin_mul_overflow(L, R, &Result)) return std::nullopt;
    return Result;
}
/// -minint is the one int64_t negation with no representable result (its
/// magnitude is 2^63, one past maxint).  Routed through checkedSub so the one
/// special case lives in a single place instead of a second copy of it here.
[[nodiscard]] inline std::optional<int64_t> checkedNeg(int64_t V) {
    return checkedSub(0, V);
}

/// EP §6.8.3.2 `i pow j` for an integer base, whose result is an integer.  Going
/// through std::pow would round anything past 2^53, so the constant folder and
/// the runtime both square-and-multiply instead.  The caller is responsible for
/// j >= 0, for which the standard defines no integer result.  nullopt on
/// overflow, same as checkedMul above (which this is built from): squaring
/// the base can overflow a step before the final multiply into Result would
/// have, so every multiplication in the loop is checked, not only the ones
/// that reach Result.
[[nodiscard]] inline std::optional<int64_t> isoPow(int64_t Base, int64_t Exp) {
    int64_t Result = 1;
    while (Exp > 0) {
        if (Exp & 1) {
            const auto Next = checkedMul(Result, Base);
            if (!Next) return std::nullopt;
            Result = *Next;
        }
        Exp >>= 1;
        if (Exp) {
            const auto Next = checkedMul(Base, Base);
            if (!Next) return std::nullopt;
            Base = *Next;
        }
    }
    return Result;
}

} // namespace plang
