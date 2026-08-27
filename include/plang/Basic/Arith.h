#pragma once

// Integer operations whose Pascal meaning differs from C's, shared by the
// constant folders and by codegen so that a folded constant and a value
// computed at run time never disagree.  Also home to the checked-arithmetic
// helpers below, shared by Sema and CodeGen for the identical reason: an
// array's declared extent is one fact, asked about independently by Sema's
// byteSizeOf and by every place CodeGen lowers an array type, and those
// answers have to agree exactly as much as a folded constant and a runtime
// value do.

#include <cstdint>
#include <optional>

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

/// The number of values in the ordinal range [Lo, Hi] -- Hi - Lo + 1 -- or
/// nullopt when that count does not fit in a uint64_t.  An array's two
/// bounds are independent int64_t values with nothing linking them, so a
/// declaration is free to write both ends of the domain at once:
/// array[low(int64)..high(int64)] asks for a count of 2**64, one more than
/// a uint64_t can even hold.  Computed directly as "Hi - Lo + 1" in
/// int64_t, that expression is signed-overflow UB for any pair far enough
/// apart -- and not just at the extreme: array[0..maxint] already overflows
/// it, since maxint IS int64_t's own upper bound (issues #214, #215).
///
/// Hi < Lo (an empty or inverted range) is not this kind of failure -- it is
/// a real, representable count of zero -- so it returns 0, not nullopt.
/// Callers that used to fold "count <= 0" into "no size to give" should keep
/// checking for that themselves; nullopt here means specifically "does not
/// fit", not "empty".
[[nodiscard]] constexpr std::optional<uint64_t> ordinalRangeCount(int64_t Lo,
                                                                   int64_t Hi) {
    if (Hi < Lo) return uint64_t{0};
    // Well-defined (unsigned arithmetic wraps instead of invoking UB) and
    // exact: casting to uint64_t preserves either value's two's-complement
    // bit pattern, and subtracting those patterns gives (Hi - Lo) mod 2**64
    // -- which IS the true difference whenever Hi >= Lo, since that
    // difference already lies in [0, 2**64) and no other value congruent to
    // it does too.
    const uint64_t Diff = static_cast<uint64_t>(Hi) - static_cast<uint64_t>(Lo);
    if (Diff == UINT64_MAX) return std::nullopt; // true count is 2**64
    return Diff + 1;
}

/// A * B, or nullopt if the exact product does not fit in a uint64_t.
/// Checked before the multiply, not after -- once a product has wrapped
/// there is no way to recover its true magnitude from the wrapped result --
/// the same idiom already used for a compile-time-constant multiply in
/// Scanner.cpp's nondecimal-literal accumulator and SchemaAccess.cpp's
/// schemaBodySize (the run-time-value case there uses LLVM's
/// umul_with_overflow intrinsic instead, since it has no host-side operands
/// to pre-check).
[[nodiscard]] constexpr std::optional<uint64_t> checkedMul64(uint64_t A, uint64_t B) {
    if (A != 0 && B > UINT64_MAX / A) return std::nullopt;
    return A * B;
}

} // namespace plang
