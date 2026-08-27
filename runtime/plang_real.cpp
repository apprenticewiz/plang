/// plang_real.cpp — ISO 7185 §6.9.3.4.1 floating-point output

#include "plang_real.h"

#include <cstdio>
#include <cstring>

namespace plang {

std::size_t plangFormatReal(char* Buf, double V, int64_t TotalWidth) {
    // §6.9.3.4.1: ActWidth is the width asked for, or the narrowest the
    // representation fits in when less was asked for.  DecPlaces is then
    // whatever is left once the fixed parts have taken their columns, which is
    // why a wider field is written to more decimal places rather than padded.
    int64_t ActWidth = TotalWidth >= PlangRealMinWidth ? TotalWidth
                                                       : PlangRealMinWidth;
    if (ActWidth > static_cast<int64_t>(PlangRealMaxChars) - 2)
        ActWidth = static_cast<int64_t>(PlangRealMaxChars) - 2;
    const int DecPlaces = static_cast<int>(ActWidth) - PlangExpDigits - 5;

    // printf's %e is the same shape with a different exponent convention: it
    // writes as many exponent digits as the value needs, with a minimum of two,
    // where the standard writes exactly ExpDigits of them.  So take the digits
    // and the exponent from it and lay the field out here.
    char Tmp[PlangRealMaxChars];
    int N = std::snprintf(Tmp, sizeof Tmp, "%.*e", DecPlaces, V);
    if (N < 0) { Buf[0] = '\0'; return 0; }
    if (static_cast<std::size_t>(N) >= sizeof Tmp) N = sizeof Tmp - 1;

    const char* Digits = Tmp;
    bool Negative = false;
    if (*Digits == '-') { Negative = true; ++Digits; }
    else if (*Digits == '+') { ++Digits; }

    const char* E = std::strchr(Digits, 'e');
    if (!E) { // no exponent to reshape: nothing sensible to do but pass it on
        std::memcpy(Buf, Tmp, static_cast<std::size_t>(N));
        return static_cast<std::size_t>(N);
    }

    const std::size_t MantLen = static_cast<std::size_t>(E - Digits);
    const char* Exp = E + 1;
    const bool  ExpNeg = *Exp == '-';
    if (*Exp == '+' || *Exp == '-') ++Exp;
    while (*Exp == '0' && *(Exp + 1) != '\0') ++Exp;  // %e pads to two; restrip
    std::size_t ExpLen = std::strlen(Exp);

    // §6.9.3.4.1: the sign character is '-' whenever the value itself was
    // negative, including -0.0.  %e always normalizes a nonzero value to a
    // leading digit of 1-9, so an all-zero mantissa here only ever means V was
    // exactly zero (positive or negative) -- not a negative value that
    // "rounded away".  FPC keeps the sign on a negative zero in every real
    // format (fixed-point and exponential alike), so match that here rather
    // than special-casing it away.
    std::size_t P = 0;
    Buf[P++] = Negative ? '-' : ' ';
    std::memcpy(Buf + P, Digits, MantLen);
    P += MantLen;
    Buf[P++] = 'e';
    Buf[P++] = ExpNeg ? '-' : '+';
    // Exactly ExpDigits of them, with leading zeros.  An exponent needing more
    // cannot arise for a double, but were the constant ever lowered, writing
    // the value in full and overrunning the field beats writing another value.
    for (std::size_t I = ExpLen; I < static_cast<std::size_t>(PlangExpDigits); ++I)
        Buf[P++] = '0';
    std::memcpy(Buf + P, Exp, ExpLen);
    P += ExpLen;
    Buf[P] = '\0';
    return P;
}

} // namespace plang
