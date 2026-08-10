/// plang_stream.h — redirectable stdin/stdout primitives for the Pascal runtime
///
/// Every runtime routine that would otherwise touch stdin or stdout goes
/// through the primitives here.  Normally they forward to the C streams, but
/// EP §6.7.5.5 defines readstr and writestr in terms of an auxiliary text file:
///
///     writestr(s, p...)  ==  rewrite(f); writeln(f, p...); reset(f); read(f, s)
///     readstr(e, v...)   ==  rewrite(f); writeln(f, e);    reset(f); read(f, v...)
///
/// Rather than duplicate every formatting and parsing routine for a string
/// destination, the primitives can be pointed at a memory buffer for the
/// duration of one of those statements.  That gives readstr and writestr the
/// same field widths, numeric formats and parsing rules as ordinary I/O,
/// which is what the equivalences above require.
///
/// The runtime is linked into Pascal programs without the C++ standard
/// library, so everything here stays within the C library.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace plang {

// ---- output ----------------------------------------------------------------

/// Appends to the capture buffer when one is active, else writes to stdout.
void plangOutN(const char* Data, std::size_t N);
void plangOutStr(const char* S);

inline void plangOutCh(int C) {
    const char Ch = static_cast<char>(C);
    plangOutN(&Ch, 1);
}

/// printf-style output through the redirect.  Formats into a buffer first so
/// the capture path and the stdout path produce identical text.
void plangOutFmt(const char* Fmt, ...) __attribute__((format(printf, 1, 2)));

// ---- input -----------------------------------------------------------------

/// When PlangInBuf is non-null, input is taken from it instead of stdin.
extern const char* PlangInBuf;
extern std::size_t PlangInLen;
extern std::size_t PlangInPos;

inline int plangInCh() {
    if (PlangInBuf)
        return (PlangInPos < PlangInLen)
                   ? static_cast<unsigned char>(PlangInBuf[PlangInPos++]) : EOF;
    return std::getchar();
}

inline void plangInUnget(int C) {
    if (C == EOF) return;
    if (PlangInBuf) { if (PlangInPos > 0) --PlangInPos; return; }
    std::ungetc(C, stdin);
}

} // namespace plang
