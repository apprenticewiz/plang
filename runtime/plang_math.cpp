/// plang_math.cpp — Pascal math built-ins (C++23)
///
/// Thin wrappers around the C++ math library.  Naming convention: the suffix
/// indicates the argument/return type in LLVM terms (_real = double,
/// _int = int64_t).
///
/// Complex functions follow the convention:
///   void plang_cXXX_out(double* re_out, double* im_out, double re, double im)
/// so that the LLVM caller can pass scalar re/im (already extracted from the
/// { double, double } aggregate) and receive results via two out-pointer
/// parameters, completely avoiding struct-return ABI complications.

#include "plang/Basic/Arith.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>

namespace plang {

extern "C" {

[[noreturn]] void plang_err_ipow_negative(int64_t E);
[[noreturn]] void plang_err_ipow_zero_zero(void);
[[noreturn]] void plang_err_ipow_overflow(int64_t Base, int64_t Exp);
[[noreturn]] void plang_err_abs_overflow(void);
[[noreturn]] void plang_err_sqr_overflow(int64_t X);
[[noreturn]] void plang_err_real_to_int_range(const char *Op, double X);
[[noreturn]] void plang_err_sqrt_domain(double X);
[[noreturn]] void plang_err_ln_domain(double X);
[[noreturn]] void plang_err_arg_domain(void);

// -std=turbo only: RandSeed's one shared definition lives in plang_sys.cpp,
// alongside plang_tp_exitcode -- see that variable's own comment for why.
// Declared here (not defined) so plang_tp_random_real/_range/
// plang_tp_randomize, below, can read and update the SAME storage every
// compiled object shares.
extern uint32_t plang_tp_randseed;

// ---- Floating-point math (double → double) ----

/// ISO §6.6.6.2: sqrt(x) is only defined for x >= 0; std::sqrt answers a
/// silent NaN for a negative argument instead of the error the standard
/// requires, the same gap abs/pow/ipow's domain checks already close for
/// their own operations.
double plang_sqrt  (double X) {
    if (X < 0.0) plang_err_sqrt_domain(X);
    return std::sqrt(X);
}
double plang_sin   (double X) { return std::sin(X);    }
double plang_cos   (double X) { return std::cos(X);    }
double plang_exp   (double X) { return std::exp(X);    }
/// ISO §6.6.6.2: ln(x) is only defined for x > 0; std::log answers NaN below
/// zero and -inf at zero instead of trapping, the same silent-domain-error
/// gap plang_sqrt closes above.  Pascal ln = natural log.
double plang_ln    (double X) {
    if (X <= 0.0) plang_err_ln_domain(X);
    return std::log(X);
}
double plang_arctan(double X) { return std::atan(X);   }
double plang_abs_real(double X) { return std::fabs(X); }

// ---- Integer math (i64 → i64) ----

/// ISO §6.6.6.2: abs(x) is x's magnitude, but int64_t's range is asymmetric
/// -- minint is -2^63, which has no positive counterpart at +2^63 -- so
/// there is exactly one input this cannot answer.  Computing '-X' for it
/// anyway is signed-overflow UB and, in practice, silently returns a second
/// wrong (and still negative) number, so trap on it instead.
int64_t plang_abs_int(int64_t X) {
    if (X == INT64_MIN) plang_err_abs_overflow();
    return X < 0 ? -X : X;
}

/// EP §6.8.3.2: i pow j, where both are integers.  The result type follows the
/// base, so this cannot go through std::pow: past 2^53 a double no longer holds
/// every integer, and the answer would come back rounded.
int64_t plang_ipow(int64_t Base, int64_t Exp) {
    // A negative exponent has no integer value except where the base is 1 or
    // -1, and EP makes the rest an error rather than a silent 0.
    if (Exp < 0) plang_err_ipow_negative(Exp);
    // EP §6.8.3.2: "an error if x is zero and y is less than or equal to
    // zero" -- y < 0 is caught above, but y = 0 is not NEGATIVE, so 0 pow 0
    // reached isoPow's loop, which never runs for Exp = 0 and answers its
    // initial Result of 1: silently, and for the one shape the standard
    // singles out as undefined before its "1 if y is zero" clause applies.
    if (Base == 0 && Exp == 0) plang_err_ipow_zero_zero();
    // isoPow's square-and-multiply, but with each multiplication checked for
    // signed overflow before it happens: like minint div -1 and abs(minint),
    // an int64 base/exponent pair the true (unbounded) result can't fit is
    // signed-overflow UB left to run its course, and in practice wraps to a
    // silently wrong value instead of the error the language expects for a
    // result outside the destination type's range.
    int64_t Result = 1;
    int64_t B       = Base;
    int64_t E       = Exp;
    while (E > 0) {
        if (E & 1) {
            if (__builtin_mul_overflow(Result, B, &Result))
                plang_err_ipow_overflow(Base, Exp);
        }
        E >>= 1;
        if (E) {
            if (__builtin_mul_overflow(B, B, &B))
                plang_err_ipow_overflow(Base, Exp);
        }
    }
    return Result;
}

// ---- sqr ----

/// ISO §6.6.6.2: sqr(x) = x*x, still an integer result for an integer
/// argument -- but not every such result fits int64_t, the same
/// signed-overflow-UB gap plang_ipow's multiplications are guarded against
/// above (and plang_abs_int's negation, just below the multiplications, guards
/// its own one fixed case).  Unlike ipow's loop this is exactly one
/// multiplication, so exactly one overflow check covers it, unguarded here it
/// is signed-overflow UB that in practice silently wraps to a negative value
/// -- issue #219.
int64_t plang_sqr_int (int64_t X) {
    int64_t Result;
    if (__builtin_mul_overflow(X, X, &Result)) plang_err_sqr_overflow(X);
    return Result;
}
double  plang_sqr_real(double  X) { return X * X; }

// ---- Conversion (double → i64) ----

// ISO §6.6.6.3: round/trunc convert a real to the integer result type, but
// not every real magnitude has an int64_t counterpart -- trunc(1e30) has no
// integer answer at all.  static_cast<int64_t> of a double outside int64's
// range is undefined behaviour, and in practice (x86-64 cvttsd2si) silently
// answers INT64_MIN, indistinguishable from that one real, legitimate
// result.  The bounds below are exact doubles (both are powers of two), so
// the comparison is exact: no value that passes it can be mis-cast.
constexpr double Int64MinAsDouble = -9223372036854775808.0; // -2^63
constexpr double Int64BoundAsDouble = 9223372036854775808.0; // 2^63 (exclusive)
static inline bool fitsInt64(double X) {
    return X >= Int64MinAsDouble && X < Int64BoundAsDouble;
}

/// Pascal trunc: toward zero.  trunc only ever shrinks a value's magnitude,
/// so checking the input itself is equivalent to checking the truncated
/// result -- there is no X for which X is out of range but trunc(X) is not.
int64_t plang_trunc(double X) {
    if (!fitsInt64(X)) plang_err_real_to_int_range("trunc", X);
    return static_cast<int64_t>(X);
}
/// Pascal round: nearest, ties away from zero.  Unlike trunc, rounding can
/// push a borderline value's magnitude up, so the range check runs on the
/// rounded result, not the raw input.
int64_t plang_round(double X) {
    double R = std::round(X);
    if (!fitsInt64(R)) plang_err_real_to_int_range("round", X);
    return static_cast<int64_t>(R);
}

// ====================================================================
// -std=turbo only: Int/Frac (real-to-real, unlike Trunc/Round above) and
// Random/Randomize (plang's own hand-rolled PRNG -- the runtime has no
// <random>, so this is a small, explicit, self-contained linear-congruential
// generator instead).  Neither claims to reproduce any real implementation's
// output: not real Borland Turbo Pascal 7's own 32-bit LCG, and not Free
// Pascal's Mersenne Twister -- the two do not agree with each other either,
// and matching either bit-for-bit is not a goal here.
// ====================================================================

/// TP Int(x) -- x's integer part, toward zero (the same direction as
/// plang_trunc/Trunc), but a REAL result, not an ordinal one.  Deliberately
/// NOT plang_trunc reused-and-cast-back: plang_trunc is range-checked
/// against int64_t because ITS result is the ordinal Integer Trunc returns,
/// a check Int's own Real result has no need of and must not inherit --
/// Int(1e30) is simply 1e30, not the runtime error Trunc(1e30) correctly is
/// (there is no int64_t for 1e30 to round-trip through in the first place).
/// std::trunc alone, with no range check of any kind, is the whole function.
double plang_tp_int(double X) { return std::trunc(X); }

/// TP Frac(x) = x - Int(x) -- x's fractional part, keeping x's own sign
/// (Frac(-3.7) is -0.7, not 0.3: there is no floor here, only the same
/// toward-zero Int just above).
double plang_tp_frac(double X) { return X - std::trunc(X); }

/// Advances plang_tp_randseed (runtime/plang_sys.cpp) one step and returns
/// the new state.  A 32-bit linear congruential generator with the
/// "Numerical Recipes" constants (a = 1664525, c = 1013904223): full period
/// 2^32, good enough statistical behavior for a general-purpose Random, and
/// simple enough to stay in plain unsigned integer arithmetic -- unsigned
/// overflow is well-defined modular wraparound in C++, exactly the
/// arithmetic an LCG wants, so this needs no overflow checking the way this
/// file's signed integer math (plang_ipow, plang_sqr_int, ...) does.
static inline uint32_t plangTpAdvanceRandSeed() {
    plang_tp_randseed = plang_tp_randseed * 1664525u + 1013904223u;
    return plang_tp_randseed;
}

/// TP Random() -- the ZERO-argument shape (Random(Range), the
/// integer-result shape, is plang_tp_random_range just below; see
/// Builtins.def's and Sema::checkCallExpr's own comments for why one source
/// name needs two runtime entry points).  Scales the generator's new 32-bit
/// state into [0, 1): 2^32 is exact in a double, so the division introduces
/// no rounding surprises, and the numerator's own range (0 .. 2^32-1) keeps
/// the result strictly below 1.0.
double plang_tp_random_real(void) {
    return static_cast<double>(plangTpAdvanceRandSeed()) / 4294967296.0; // 2^32
}

/// TP Random(Range) -- the ONE-argument shape.  Range == 0 has no [0, Range)
/// (or (Range, 0]) to answer from, so that case alone stays a flat 0 with no
/// generator advance, the same "always in range, never a crash" answer 0
/// already is for every genuinely in-range Range.  A NEGATIVE Range is NOT
/// the same "answer 0" case (issue #676): `fpc -Mtp` 3.2.2 answers a
/// deterministic, nonzero-capable value in (Range, 0], not a flat 0.
///
/// The magnitude is computed with the identical unsigned widening multiply-
/// and-shift the positive-Range path already used, against |Range| rather
/// than Range itself, and then negated for a negative Range -- NOT a signed
/// multiply against Range directly performing its own floor division (an
/// earlier version of this fix tried that, on the theory that `fpc -Mtp`
/// computes `random(-R)` and `random(R)` from the same generator state with
/// a shared floor(S*Range / 2^32) formula; empirically, against many Range
/// values and RandSeeds, `fpc -Mtp` visibly does NOT hold to that relationship
/// -- its own random(-R) and random(R) pairs disagree with a pure floor-
/// division reading of each other often enough that a signed-multiply
/// implementation occasionally answered exactly Range itself, e.g.
/// random(-50) = -50, a boundary value never in (Range, 0) -- see this
/// project's own README on why plang's generator does not claim to
/// reproduce ANY real implementation's sequence bit-for-bit).  Reusing the
/// positive path's own magnitude computation against |Range| instead keeps
/// the same guarantee the positive path already has -- the result's
/// magnitude is always STRICTLY less than |Range|, so a negative Range can
/// never answer Range itself, only strictly inside (Range, 0].
int64_t plang_tp_random_range(int64_t Range) {
    if (Range == 0) return 0;
    const uint32_t S = plangTpAdvanceRandSeed();
    const bool Neg = Range < 0;
    const uint64_t AbsRange =
        Neg ? static_cast<uint64_t>(-Range) : static_cast<uint64_t>(Range);
    const int64_t Magnitude = static_cast<int64_t>(
        (static_cast<uint64_t>(S) * AbsRange) >> 32);
    return Neg ? -Magnitude : Magnitude;
}

/// TP Randomize -- reseeds RandSeed from wall-clock time, so successive RUNS
/// of the same program get a different Random sequence.  clock_gettime
/// (CLOCK_REALTIME), not plang_gettimestamp/time_t (plang_time.cpp): that
/// pair's one-second resolution would make two runs started in the same
/// second seed identically, which defeats the entire point of Randomize.
/// clock_gettime is a POSIX C API available through <ctime> -- no <random>
/// or other C++ stdlib facility needed, consistent with the rest of this
/// runtime (see this file's own header comment).  tv_sec is spread with a
/// Knuth multiplicative-hash constant before combining with tv_nsec, so nsec
/// alone (which on some platforms/clocks advances in coarse steps) does not
/// dominate the result.
void plang_tp_randomize(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    const uint32_t Sec  = static_cast<uint32_t>(ts.tv_sec);
    const uint32_t Nsec = static_cast<uint32_t>(ts.tv_nsec);
    plang_tp_randseed = (Sec * 2654435761u) ^ Nsec;
}

// ====================================================================
// EP §6.4.2.2 / §6.7.6.2–3: Complex-number support
// Convention: (re_out, im_out, re_in, im_in) for complex→complex;
//             (re, im)→double for complex→real functions.
// ====================================================================

// Helper: map (re,im) scalars to std::complex<double>
static inline std::complex<double> plang_mk(double re, double im) {
    return std::complex<double>(re, im);
}

/// EP §6.7.6.2: arg(z) = atan2(im, re), the phase angle of z -- undefined at
/// the origin, where every angle is equally valid; std::atan2(0, 0) answers a
/// silent 0 by convention instead of the error the runtime's other domain
/// checks (plang_sqrt, plang_ln, plang_err_cpow_domain's zero complex base)
/// give a program instead of an ordinary in-range result -- issue #249.
/// CGFuncCall.cpp routes a real/integer argument through this same function
/// as (x, 0.0), so a plain real/integer 0 -- the origin under the same
/// definition -- traps here too, not just cmplx(0, 0).
double plang_arg(double re, double im) {
    if (re == 0.0 && im == 0.0) plang_err_arg_domain();
    return std::atan2(im, re);
}

// EP §6.7.6.2: |z| = sqrt(re^2 + im^2)
double plang_abs_cplx(double re, double im) {
    return std::abs(plang_mk(re, im));
}

// Complex transcendentals — each writes into (re_out, im_out).

void plang_csqrt_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::sqrt(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}
void plang_csin_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::sin(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}
void plang_ccos_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::cos(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}
void plang_cexp_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::exp(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}
void plang_cln_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::log(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}
void plang_carctan_out(double* re_out, double* im_out, double re, double im) {
    auto r = std::atan(plang_mk(re, im));
    *re_out = r.real(); *im_out = r.imag();
}

// EP §6.8.3.2: complex power  a ** b = exp(b * ln(a))
void plang_cpow_out(double* re_out, double* im_out,
                    double are, double aim, double bre, double bim) {
    auto r = std::pow(plang_mk(are, aim), plang_mk(bre, bim));
    *re_out = r.real(); *im_out = r.imag();
}

} // extern "C"

} // namespace plang
