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
#include <utility>

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

/// The bounds of a Width-bit ordinal, signed or not, as int64_t -- every
/// width this compiler stamps (8/16/32/64, see Type::Width) fits its full
/// range in an int64_t, so the checked-arithmetic helpers below can compare
/// an int64_t-domain result against these directly instead of working in a
/// narrower machine type.  Width == 64 signed is deliberately exact-fit
/// (Lo == INT64_MIN, Hi == INT64_MAX): shifting `1LL << 63` is itself signed
/// overflow, so that case is special-cased rather than run through the same
/// shift as the others.
[[nodiscard]] constexpr std::pair<int64_t, int64_t> narrowIntBounds(unsigned Width, bool Signed) {
    if (!Signed) return {0, Width >= 64 ? INT64_MAX : (int64_t{1} << Width) - 1};
    if (Width >= 64) return {INT64_MIN, INT64_MAX};
    const int64_t Half = int64_t{1} << (Width - 1);
    return {-Half, Half - 1};
}

/// Checked +, -, *: nullopt on overflow, rather than the UB a plain
/// `+`/`-`/`*` would be there (a sanitizer abort during development, a
/// silently wrapped value in a release build otherwise).  A constant folder
/// that overflows must decline the fold and hand nothing back, not the
/// wrapped value, as though it were the constant the source actually named.
///
/// \p Width and \p Signed are the result's Type::Width/IsSigned.  Both
/// default to what ISO 7185 and Extended Pascal's one Integer type always
/// is -- 64-bit signed -- so every call site written before Turbo (there is
/// no other kind yet) keeps computing exactly the int64_t-overflow check it
/// always has.  Turbo's narrower Integer/Byte/Word/etc. additionally have to
/// fail where a plang::Type of that width could not hold the mathematically
/// exact result even though it fits in int64_t -- 30000 + 30000 does not
/// overflow int64_t, but it does overflow Turbo's 16-bit Integer, and a
/// constant folder that let it through would agree with neither Turbo Pascal
/// nor with what codegen's checked arithmetic (once it exists for div/mod)
/// does to the same expression at run time.
[[nodiscard]] inline std::optional<int64_t> checkedAdd(int64_t L, int64_t R,
                                                         unsigned Width = 64,
                                                         bool Signed = true) {
    int64_t Result;
    if (__builtin_add_overflow(L, R, &Result)) return std::nullopt;
    if (Width < 64) {
        const auto [Lo, Hi] = narrowIntBounds(Width, Signed);
        if (Result < Lo || Result > Hi) return std::nullopt;
    }
    return Result;
}
[[nodiscard]] inline std::optional<int64_t> checkedSub(int64_t L, int64_t R,
                                                         unsigned Width = 64,
                                                         bool Signed = true) {
    int64_t Result;
    if (__builtin_sub_overflow(L, R, &Result)) return std::nullopt;
    if (Width < 64) {
        const auto [Lo, Hi] = narrowIntBounds(Width, Signed);
        if (Result < Lo || Result > Hi) return std::nullopt;
    }
    return Result;
}
[[nodiscard]] inline std::optional<int64_t> checkedMul(int64_t L, int64_t R,
                                                         unsigned Width = 64,
                                                         bool Signed = true) {
    int64_t Result;
    if (__builtin_mul_overflow(L, R, &Result)) return std::nullopt;
    if (Width < 64) {
        const auto [Lo, Hi] = narrowIntBounds(Width, Signed);
        if (Result < Lo || Result > Hi) return std::nullopt;
    }
    return Result;
}
/// -minint is the one negation with no representable result at any width
/// (its magnitude is one past maxint, same shape whether Width is 64 or
/// narrower).  Routed through checkedSub so the one special case lives in a
/// single place instead of a second copy of it here.
[[nodiscard]] inline std::optional<int64_t> checkedNeg(int64_t V,
                                                         unsigned Width = 64,
                                                         bool Signed = true) {
    return checkedSub(0, V, Width, Signed);
}

/// EP §6.8.3.2 `i pow j` for an integer base, whose result is an integer.  Going
/// through std::pow would round anything past 2^53, so the constant folder and
/// the runtime both square-and-multiply instead.  The caller is responsible for
/// j >= 0, for which the standard defines no integer result.  nullopt on
/// overflow, same as checkedMul above (which this is built from, and from
/// which \p Width/\p Signed take their meaning and default): squaring
/// the base can overflow a step before the final multiply into Result would
/// have, so every multiplication in the loop is checked, not only the ones
/// that reach Result.
[[nodiscard]] inline std::optional<int64_t> isoPow(int64_t Base, int64_t Exp,
                                                     unsigned Width = 64,
                                                     bool Signed = true) {
    int64_t Result = 1;
    while (Exp > 0) {
        if (Exp & 1) {
            const auto Next = checkedMul(Result, Base, Width, Signed);
            if (!Next) return std::nullopt;
            Result = *Next;
        }
        Exp >>= 1;
        if (Exp) {
            const auto Next = checkedMul(Base, Base, Width, Signed);
            if (!Next) return std::nullopt;
            Base = *Next;
        }
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
