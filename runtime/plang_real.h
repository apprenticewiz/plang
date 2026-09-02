/// plang_real.h — ISO 7185 §6.9.3.4.1 floating-point output
///
/// Pascal writes a real in a shape of its own, and it is not one printf has a
/// conversion for.  §6.9.3.1 makes `write(f, e)` mean `write(f, e: TotalWidth)`
/// with a default width, and §6.9.3.4.1 gives that form as
///
///     sign  digit  '.'  DecPlaces digits  expchar  expsign  ExpDigits digits
///
/// so a real always carries an exponent unless a fraction length asked for the
/// fixed-point form, and the sign is a character of the field rather than
/// something omitted when positive.  The two differences from C that matter:
/// the width sets the *precision* rather than a pad, DecPlaces growing to fill
/// whatever field was asked for; and a positive value still occupies its sign
/// column, so columns of numbers line up.
///
/// Both the text runtime and the file runtime write reals, and they write to
/// different destinations, so the shape is rendered into a caller's buffer here
/// and each writes it out its own way.

#pragma once

#include <cstddef>
#include <cstdint>

namespace plang {

/// The number of digit-characters in an exponent (§6.9.3.4.1, ExpDigits).  The
/// standard leaves this to the implementation and asks only that it be fixed,
/// with leading zeros where the value needs them.  Three covers the whole range
/// an IEEE double reaches, so no value has to be written in a shape other than
/// the one advertised here.
inline constexpr int PlangExpDigits = 3;

/// The default TotalWidth for a real written without one (§6.9.3.1).  Also
/// implementation-defined.  This one leaves DecPlaces = 16, so seventeen
/// significant digits are written -- the minimum needed (Steele & White) to
/// round-trip any IEEE-754 double exactly, and what FPC itself writes by
/// default.  Fewer digits (this used to be 15) lets a value near DBL_MAX
/// round up past the representable range when printed, so reading it back
/// produces +Infinity instead of the original value.
///
/// Turbo shares this exact value too (see PlangRealProfileTurbo below): a
/// `real` variable's default write field, checked directly against
/// `fpc -Mtp` (which maps Turbo's `real` onto Double, the same 8-byte type
/// this runtime always uses -- Extended is a different, 10-byte type FPC
/// gives only an explicitly-declared `extended`, never a bare literal's
/// default write), comes out byte-for-byte identical to ISO's own down to
/// the decimal-place count -- the two dialects disagree only on which
/// letter the exponent uses, not on how wide the field is.
inline constexpr int PlangRealWidth = 24;

/// The smallest field the representation fits in: the sign, a digit, the point,
/// one decimal, the exponent character, its sign and its digits.
inline constexpr int PlangRealMinWidth = PlangExpDigits + 6;

/// The most characters formatReal can produce, for a caller sizing a buffer.
/// A width beyond this is met by writing this many, since the digits past the
/// seventeenth of a double are not the value's own anyway.
inline constexpr std::size_t PlangRealMaxChars = 512;

/// Turbo's Single (32-bit IEEE-754 binary32) is promoted to double in CodeGen
/// before it ever reaches this formatter -- the runtime only ever sees a
/// double, and there is only one formatter for it -- but a binary32's own
/// precision is nine significant decimal digits (Steele & White's round-trip
/// count for the format), not a double's seventeen.  Formatting a promoted
/// Single with DecPlaces=16 the way a genuine double gets does not print MORE
/// of the value; it prints the double-promotion's own zero-padding-turned-
/// nonzero tail, bits the original binary32 never had an opinion about, as if
/// they were significant.  Eight decimal places (one leading digit + eight)
/// is what plangFormatReal's MaxDecPlaces caps a Single write to.
inline constexpr int PlangSingleMaxDecPlaces = 8;

/// What varies between a dialect's default real-write shape.  ISO 7185 and
/// Extended Pascal share one profile; Turbo's is a second, DIFFERENT default
/// -- CodeGen picks which one to pass based on LangOptions.turbo(), never a
/// runtime-side dialect check (the runtime has no such thing to check: an
/// ISO object file and a Turbo one can be linked into the same binary, so
/// nothing here may depend on a process-global "which dialect" state).
///
/// Only ExpChar actually differs today (see PlangRealWidth's own comment for
/// how that was confirmed against `fpc -Mtp`) -- Width/ExpDigits are broken
/// out as their own fields anyway, rather than folding the whole profile
/// down to a single bool, so that a future dialect whose default width or
/// exponent-digit count genuinely differs has somewhere to say so without
/// another parameter threaded through every call site again.
struct PlangRealProfile {
    int  Width;     // Default TotalWidth (PlangRealWidth's role, parameterized).
    int  ExpDigits;  // Fixed exponent digit count (PlangExpDigits's role).
    char ExpChar;    // 'e' (ISO 7185 / EP) or 'E' (Turbo).
};

/// ISO 7185 / EP: today's exact values, unchanged -- see plangFormatReal's
/// own comment for why this one's shape is the load-bearing correctness
/// property of the whole parameterization.
inline constexpr PlangRealProfile PlangRealProfileISO{PlangRealWidth, PlangExpDigits, 'e'};
/// Turbo: identical width and exponent-digit count to ISO's (see
/// PlangRealWidth's comment), differing only in the exponent letter's case.
inline constexpr PlangRealProfile PlangRealProfileTurbo{PlangRealWidth, PlangExpDigits, 'E'};

/// Renders \p V into \p Buf in the floating-point representation of
/// §6.9.3.4.1, in a field of \p TotalWidth characters, and returns how many
/// were written.  \p Buf holds at least PlangRealMaxChars.  A TotalWidth below
/// the minimum is raised to it, as the standard's ActWidth does.
///
/// \p Profile's DEFAULT ARGUMENT is PlangRealProfileISO precisely so every
/// call site this project had before Turbo existed -- there were no others,
/// since plang had only one dialect family's real format until now --
/// continues to build the identical shape it always did without being
/// touched: the profile parameterization is additive, not a rewrite of the
/// ISO path.  A Turbo call site passes PlangRealProfileTurbo explicitly.
///
/// \p MaxDecPlaces caps how many decimal digits a wide field is allowed to
/// grow DecPlaces to -- see PlangSingleMaxDecPlaces's own comment for why a
/// promoted Single needs this and a genuine double (every call site before
/// Single existed) does not.  Its DEFAULT ARGUMENT is -1 ("no cap") for the
/// identical reason \p Profile's is PlangRealProfileISO: every pre-Single call
/// site keeps building the exact shape it always did.  When the cap applies
/// and leaves the rendered value narrower than ActWidth, the shortfall is
/// filled with LEADING SPACES rather than more digits -- checked against
/// `fpc -Mtp`'s own Single formatting, which pads the same way rather than
/// ever inventing precision a binary32 does not have.
std::size_t plangFormatReal(char* Buf, double V, int64_t TotalWidth,
                             const PlangRealProfile& Profile = PlangRealProfileISO,
                             int MaxDecPlaces = -1);

/// issue #677: `write`/`Str`'s FIXED-point form (`v:W:D`, §6.9.3.4.2, printf's
/// `%*.*f` before this) used to hand V straight to printf, which prints a
/// double's EXACT binary value out to however many decimals D asks for --
/// correct arithmetic, but not what any Pascal reader expects, since a
/// double's own precision runs out at 15-17 significant decimal digits: past
/// that point printf is not revealing more of the VALUE the program wrote, it
/// is revealing bits of the binary REPRESENTATION nothing meaningful chose
/// (`str(1e30:3:2, s)` used to answer '...19884624838656.00', not
/// '...00000000000.00').  `fpc -Mtp` does not do this either -- it stops
/// short of the exact expansion -- so a request whose OWN significant-digit
/// span exceeds 17 (the Steele & White round-trip count PlangRealWidth's own
/// comment already cites for the exponential form) is capped to that many,
/// with every digit position beyond treated as 0 -- the same "do not invent
/// precision a double never had" rule plangFormatReal's own MaxDecPlaces
/// already applies to a promoted Single.
///
/// plangRealFixedNeedsCap decides whether THIS particular (V, FracDigits)
/// pair needs that capping at all: whenever Exp(V) + FracDigits + 1 -- the
/// number of significant digits a request spanning from V's own leading
/// digit down through its FracDigits-th decimal place would show -- is 17 or
/// fewer, plain printf's `%*.*f` already gets it exactly right, INCLUDING
/// correctly ROUNDING at the FracDigits-th place even when FracDigits asks
/// for a COARSER position than V's own leading digit (`0.06:1:1` must answer
/// "0.1", not "0.0" -- only %.*f's own rounding gets that right; recomputing
/// it via a FIXED-significant-digit %.*e independent of FracDigits would not
/// even see the digit the rounding decision depends on).  It returns false
/// in that case, touching \p Shape not at all -- every call site's existing
/// `%*.*f` call stays completely unchanged for it, which is also every
/// "normal", non-pathological write.  Only once the span exceeds 17 --
/// exactly the regime where the exact-binary-expansion noise this item
/// fixes can appear at all -- does it return true, with \p Shape rounded to
/// 17 significant digits (via `%.16e`) for plangRealFixedDigitAt below to
/// answer from.
///
/// plangRealFixedDigitAt answers the decimal digit at a given power-of-ten
/// \p Weight (0 = units, -1 = the first digit after the point, 1 = tens,
/// ...) -- a significant digit where \p Weight falls within the rounded
/// value's own 17-digit span, '0' everywhere else.  Split into "decide/
/// compute" and "query" rather than building a formatted string directly:
/// each of this project's three fixed-point call sites (stdout, ISO/EP
/// file, Turbo file) writes to a different destination (plangOutCh, two
/// different PascalFile*s) with its own existing field-width/padding loop
/// already in place, so the capped path only needs to supply the DIGITS
/// each loop asks for, not a second copy of the loop itself -- and needs no
/// buffer sized for an arbitrary TotalWidth/FracDigits (unlike
/// plangFormatReal's PlangRealMaxChars) since a digit is produced one query
/// at a time, however wide the field goes, exactly the way each caller's
/// OWN loop already paces its padding one character at a time (issue #247's
/// guard).
///
/// Not used for non-finite V (Inf/NaN): every call site keeps its original
/// `%*.*f` for that case, checked with std::isfinite before ever calling
/// this -- "extending Inf/NaN's own precision" is not a meaningful question
/// in the first place.
struct PlangRealFixedShape {
    bool Negative;    // std::signbit(V); an all-zero Digits does not imply +0.
    int  Exp;         // Digits[k]'s own decimal weight is Exp - k.
    char Digits[17];  // V's 17-significant-digit rounding, most significant first.
};

bool plangRealFixedNeedsCap(double V, int64_t FracDigits, PlangRealFixedShape* Shape);

inline char plangRealFixedDigitAt(const PlangRealFixedShape& Shape, int64_t Weight) {
    const int64_t K = Shape.Exp - Weight;
    return (K >= 0 && K <= 16) ? Shape.Digits[static_cast<std::size_t>(K)] : '0';
}

/// The number of digits the shape's integer part (weights >= 0) has: always
/// at least 1, so a value under 1.0 still gets its leading "0" -- the same
/// convention std::isfinite's %f fallback (and every dialect's own field
/// practice) already uses.
inline int64_t plangRealFixedIntDigits(const PlangRealFixedShape& Shape) {
    return Shape.Exp >= 0 ? static_cast<int64_t>(Shape.Exp) + 1 : 1;
}

} // namespace plang
