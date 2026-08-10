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
/// implementation-defined.  This one leaves DecPlaces = 14, so fifteen
/// significant digits are written.
inline constexpr int PlangRealWidth = 22;

/// The smallest field the representation fits in: the sign, a digit, the point,
/// one decimal, the exponent character, its sign and its digits.
inline constexpr int PlangRealMinWidth = PlangExpDigits + 6;

/// The most characters formatReal can produce, for a caller sizing a buffer.
/// A width beyond this is met by writing this many, since the digits past the
/// seventeenth of a double are not the value's own anyway.
inline constexpr std::size_t PlangRealMaxChars = 512;

/// Renders \p V into \p Buf in the floating-point representation of
/// §6.9.3.4.1, in a field of \p TotalWidth characters, and returns how many
/// were written.  \p Buf holds at least PlangRealMaxChars.  A TotalWidth below
/// the minimum is raised to it, as the standard's ActWidth does.
std::size_t plangFormatReal(char* Buf, double V, int64_t TotalWidth);

} // namespace plang
