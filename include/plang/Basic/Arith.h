#pragma once

// Integer operations whose Pascal meaning differs from C's, shared by the
// constant folders and by codegen so that a folded constant and a value
// computed at run time never disagree.

#include <cstdint>

namespace plang {

/// ISO §6.7.2.2 `i mod j`, whose result is always in [0, j).  C's `%` takes its
/// sign from the dividend instead, so (-17) % 5 is -2 where Pascal wants 3.
/// The caller is responsible for j > 0, which the standard also requires.
[[nodiscard]] constexpr int64_t isoMod(int64_t I, int64_t J) {
    const int64_t R = I % J;
    return R < 0 ? R + (J < 0 ? -J : J) : R;
}

/// EP §6.8.3.2 `i pow j` for an integer base, whose result is an integer.  Going
/// through std::pow would round anything past 2^53, so the constant folder and
/// the runtime both square-and-multiply instead.  The caller is responsible for
/// j >= 0, for which the standard defines no integer result.
[[nodiscard]] constexpr int64_t isoPow(int64_t Base, int64_t Exp) {
    int64_t Result = 1;
    while (Exp > 0) {
        if (Exp & 1) Result *= Base;
        Exp >>= 1;
        if (Exp) Base *= Base;
    }
    return Result;
}

} // namespace plang
