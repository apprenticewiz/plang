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

namespace plang {

extern "C" {

[[noreturn]] void plang_err_ipow_negative(int64_t E);
[[noreturn]] void plang_err_ipow_zero_zero(void);
[[noreturn]] void plang_err_abs_overflow(void);

// ---- Floating-point math (double → double) ----

double plang_sqrt  (double X) { return std::sqrt(X);   }
double plang_sin   (double X) { return std::sin(X);    }
double plang_cos   (double X) { return std::cos(X);    }
double plang_exp   (double X) { return std::exp(X);    }
double plang_ln    (double X) { return std::log(X);    } ///< Pascal ln = natural log
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
    return isoPow(Base, Exp);
}

// ---- sqr ----

int64_t plang_sqr_int (int64_t X) { return X * X; }
double  plang_sqr_real(double  X) { return X * X; }

// ---- Conversion (double → i64) ----

int64_t plang_trunc(double X) { return static_cast<int64_t>(X); }            ///< Pascal trunc: toward zero
int64_t plang_round(double X) { return static_cast<int64_t>(std::round(X)); } ///< Pascal round: nearest, ties away from zero

// ====================================================================
// EP §6.4.2.2 / §6.7.6.2–3: Complex-number support
// Convention: (re_out, im_out, re_in, im_in) for complex→complex;
//             (re, im)→double for complex→real functions.
// ====================================================================

// Helper: map (re,im) scalars to std::complex<double>
static inline std::complex<double> plang_mk(double re, double im) {
    return std::complex<double>(re, im);
}

// EP §6.7.6.2: arg(z) = atan2(im, re)
double plang_arg(double re, double im) {
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
