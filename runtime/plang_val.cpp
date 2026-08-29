/// plang_val.cpp — Turbo Val(s, v, code): a non-fatal string-to-number
/// parser (C++23)
///
/// Every OTHER numeric-parsing entry point in this runtime (plang_read_i64/
/// _f64 and their _turbo siblings, plang_io.cpp) is [[noreturn]]-fatal on
/// malformed input: they call plang_err_read_format/plang_err_read_int_range
/// or plang_tp_runerror, all of which std::exit the process.  Val's own
/// contract is the opposite and is the entire reason this is a new, separate
/// file rather than an extension of plang_io.cpp's existing scanners: a
/// `Code` OUTPUT PARAMETER is 0 on success, or the 1-based index of the
/// first character that does not fit Val's grammar on failure, and control
/// returns to the caller EITHER way -- never a process exit.
///
/// Written as two clean, Val-specific primitives (plang_val_parse_int/
/// _real) rather than folded into CGProcCall's own Val lowering, so a LATER,
/// separate Tier 3 work item (IOResult/InOutRes, not implemented here and
/// out of this item's scope) can reuse this exact non-fatal-parse shape for
/// its own read()-side error reporting instead of inventing a second one.
///
/// Every rule below was checked against a local `fpc -Mtp` install; see the
/// PR this shipped in for the full transcript.  Two DELIBERATE, documented
/// divergences from fpc's own exact behavior (both still fully within Val's
/// contract: non-fatal, and a defensible Code on failure) are noted at their
/// own call sites below -- fpc's real-number scanner has an internal
/// backtracking quirk around a trailing 'e'/'e+'/'e-' with no exponent
/// digits that could not be reduced to a general rule from the cases tried
/// (`Val('1e', ...)` fails at Length+1, but `Val('1e+', ...)` SUCCEEDS,
/// silently discarding the 'e+' -- inconsistent even within fpc itself), and
/// this file's single-pass, non-backtracking scanner instead uniformly fails
/// both at the position right after the introducer.

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace plang {

namespace {

inline bool isValSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/// The digit value of \p c in \p radix, or -1 if \p c is not a digit of it.
/// Letters are accepted case-insensitively (hex 'F' and 'f' alike), which is
/// the only radix here wide enough for letters to matter.
inline int valDigit(char c, int radix) {
    int d;
    if (c >= '0' && c <= '9')      d = c - '0';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else return -1;
    return (d < radix) ? d : -1;
}

} // namespace

extern "C" {

/// Parses [Data, Data+Len) as a signed integer literal: optional leading
/// blanks, an optional '+'/'-' sign, an optional radix prefix ($ or x/X for
/// hex -- 'x12' and '0x12' both mean hex 0x12 = 18, confirmed against
/// `fpc -Mtp`; '&' for octal; '%' for binary), then one or more digits of
/// that radix -- the ENTIRE remainder of the string must be consumed for
/// success, matching `fpc -Mtp` field practice ('123  ' with trailing spaces
/// fails at the first trailing space, not silently accepted).
///
/// \p IsUnsigned rejects a leading '-' outright (Code = 2, the position
/// right after the sign) even for magnitude 0 -- confirmed against
/// `fpc -Mtp`: Val('-0', aWordVar, code) fails the same way Val('-1', ...)
/// does.  A '+' sign is accepted for either signedness.
///
/// On success, *OutVal holds the FULL int64 magnitude with its sign applied
/// -- overflow of int64 ITSELF (not the eventual destination's own, likely
/// narrower, width) is what Code reports on a magnitude that is simply too
/// big: confirmed against `fpc -Mtp` that a 21-digit literal fails at digit
/// 19, exactly where a signed int64 accumulator overflows, regardless of the
/// destination variable's own declared type. A value that overflows the
/// DESTINATION's width but fits int64 is instead a Code = 0 SUCCESS whose
/// *OutVal simply does not fit -- the caller (CGProcCall.cpp) truncates it
/// into the destination with an ordinary LLVM sign-extend-or-truncate, which
/// reproduces fpc's own silent 2's-complement wraparound exactly (Val('40000',
/// aTurboIntegerVar, code) -> code = 0, value wrapped to -25536, confirmed).
void plang_val_parse_int(const char* Data, int64_t Len, int8_t IsUnsigned,
                          int64_t* OutVal, int64_t* OutCode) {
    if (!Data) Len = 0;
    int64_t pos = 0;
    while (pos < Len && isValSpace(Data[pos])) ++pos;

    int sign = 1;
    if (pos < Len && (Data[pos] == '+' || Data[pos] == '-')) {
        if (Data[pos] == '-') {
            if (IsUnsigned) { *OutVal = 0; *OutCode = pos + 2; return; }
            sign = -1;
        }
        ++pos;
    }

    int radix = 10;
    if (pos < Len) {
        if (Data[pos] == '$') { radix = 16; ++pos; }
        else if (Data[pos] == 'x' || Data[pos] == 'X') { radix = 16; ++pos; }
        else if (Data[pos] == '0' && pos + 1 < Len
                 && (Data[pos + 1] == 'x' || Data[pos + 1] == 'X')) { radix = 16; pos += 2; }
        else if (Data[pos] == '&') { radix = 8; ++pos; }
        else if (Data[pos] == '%') { radix = 2; ++pos; }
    }

    const int64_t digitsStart = pos;
    uint64_t mag = 0;
    int64_t overflowAt = -1;
    while (pos < Len) {
        const int d = valDigit(Data[pos], radix);
        if (d < 0) break;
        if (overflowAt < 0) {
            const auto ud = static_cast<uint64_t>(d);
            const auto ur = static_cast<uint64_t>(radix);
            if (mag > (static_cast<uint64_t>(INT64_MAX) - ud) / ur)
                overflowAt = pos;
            else
                mag = mag * ur + ud;
        }
        ++pos;
    }

    if (overflowAt >= 0) { *OutVal = 0; *OutCode = overflowAt + 1; return; }
    if (pos == digitsStart) { *OutVal = 0; *OutCode = pos + 1; return; } // no digits at all
    if (pos != Len)         { *OutVal = 0; *OutCode = pos + 1; return; } // trailing junk

    *OutVal  = (sign < 0) ? -static_cast<int64_t>(mag) : static_cast<int64_t>(mag);
    *OutCode = 0;
}

/// Parses [Data, Data+Len) as a real literal: optional leading blanks, an
/// optional '+'/'-' sign, decimal digits, an optional '.' fraction (more
/// decimal digits), an optional 'e'/'E' exponent (an optional sign and one
/// or more decimal digits -- REQUIRED once 'e'/'E' appears at all) -- the
/// entire remainder must be consumed, same rule as the integer form.  No
/// radix-prefix extension applies here (Val('$FF', aRealVar, code) fails at
/// position 1, confirmed against `fpc -Mtp`) -- Turbo's hex/octal/binary
/// literals are integer-only.
///
/// See this file's own header comment for the one confirmed, deliberate
/// divergence from fpc's exact behavior (a trailing 'e'/'e+'/'e-' with no
/// exponent digits).
void plang_val_parse_real(const char* Data, int64_t Len, double* OutVal, int64_t* OutCode) {
    if (!Data) Len = 0;
    int64_t pos = 0;
    while (pos < Len && isValSpace(Data[pos])) ++pos;

    const int64_t start = pos;
    if (pos < Len && (Data[pos] == '+' || Data[pos] == '-')) ++pos;

    const int64_t intDigitsStart = pos;
    while (pos < Len && Data[pos] >= '0' && Data[pos] <= '9') ++pos;
    const bool hadIntDigits = pos > intDigitsStart;

    bool hadFracDigits = false;
    if (pos < Len && Data[pos] == '.') {
        ++pos;
        const int64_t fracStart = pos;
        while (pos < Len && Data[pos] >= '0' && Data[pos] <= '9') ++pos;
        hadFracDigits = pos > fracStart;
    }

    if (!hadIntDigits && !hadFracDigits) { *OutVal = 0.0; *OutCode = pos + 1; return; }

    if (pos < Len && (Data[pos] == 'e' || Data[pos] == 'E')) {
        ++pos;
        if (pos < Len && (Data[pos] == '+' || Data[pos] == '-')) ++pos;
        const int64_t expStart = pos;
        while (pos < Len && Data[pos] >= '0' && Data[pos] <= '9') ++pos;
        if (pos == expStart) { *OutVal = 0.0; *OutCode = pos + 1; return; }
    }

    if (pos != Len) { *OutVal = 0.0; *OutCode = pos + 1; return; }

    // [start, pos) is exactly the span strtod needs to see; copy it to a
    // NUL-terminated buffer since strtod has no length-bounded form. 64
    // bytes covers every ordinary literal without allocating; a pathological
    // one (hundreds of digits) falls back to malloc rather than truncating.
    char stackBuf[64];
    const int64_t n = pos - start;
    char* buf = stackBuf;
    const bool heap = static_cast<size_t>(n) >= sizeof(stackBuf);
    if (heap) buf = static_cast<char*>(std::malloc(static_cast<size_t>(n) + 1));
    if (!buf) { *OutVal = 0.0; *OutCode = 1; return; } // allocation failed: report, don't crash
    std::memcpy(buf, Data + start, static_cast<size_t>(n));
    buf[n] = '\0';
    *OutVal = std::strtod(buf, nullptr);
    if (heap) std::free(buf);
    *OutCode = 0;
}

} // extern "C"

} // namespace plang
