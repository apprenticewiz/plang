/// plang_sstr.cpp — Turbo string[N] (ShortString) runtime (C++23)
///
/// Layout of a string[N] variable in memory:
///   byte   0   : uint8_t  length   — current number of characters (0..N,
///                                    and never more than 255: the field
///                                    that holds it is one byte wide)
///   bytes  1.. : char     data[N]  — character data (NOT null-terminated)
///
/// This is a DIFFERENT, INCOMPATIBLE layout from EP's string(N)
/// (plang_str.cpp): a one-byte length prefix here, not eight, and 1-byte
/// alignment throughout (CGTypes::sstrStructType builds it PACKED).  Every
/// function below takes a (ptr, capacity) pair the same shape plang_str.cpp's
/// do, so a single set of functions serves every N -- but nothing here reads
/// or writes an i64 header, and nothing here is called with a string(N)
/// pointer or vice versa.
///
/// SCOPE (see the work item this shipped with): this file gives ShortString's
/// TYPE/LAYOUT existence the minimal runtime support a bare declaration and a
/// basic, non-truncating write/read round-trip need to not crash.  It does
/// NOT implement Turbo Pascal's actual string[N] semantics -- truncating
/// assignment, prefix/space-padded comparison, s[0] as the length byte,
/// concatenation clamping, Copy/Pos/Delete/Insert/Str/Val, or parameter-copy-
/// at-callee-width.  Those are a separate, later work item that builds on top
/// of this one; do not extend this file with them without reading that item's
/// own scope first.

#include "plang_stream.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace plang {

namespace {
inline uint8_t&       sstrLen(void* s)             { return *reinterpret_cast<uint8_t*>(s); }
inline uint8_t         sstrLen(const void* s)       { return *reinterpret_cast<const uint8_t*>(s); }
inline char*           sstrData(void* s)            { return reinterpret_cast<char*>(s) + 1; }
inline const char*     sstrData(const void* s)      { return reinterpret_cast<const char*>(s) + 1; }
// A ShortString's length field is one byte wide, so no in-bounds length can
// ever exceed 255 regardless of the DECLARED capacity -- see SemaType.cpp's
// ShortString byteSizeOf/byteAlignOf comment for why this item deliberately
// does not also cap N itself at 255 (that is real Turbo/FPC field practice,
// but it is a language rule for a later item, not a memory-safety
// requirement of this one).  Every function here clamps the capacity it
// actually uses to this ceiling, so a `string[huge N]` -- legal to declare
// per this item's scope, and caught instead by Sema's existing declaration-
// size gate when N is unreasonably large -- can never make the byte header
// try to represent more than it can hold.
constexpr int64_t kMaxLen = 255;
inline int64_t effCap(int64_t cap) { return std::min(cap, kMaxLen); }
} // namespace

extern "C" {

/// Defined with the other runtime error reporters in plang_sys.cpp; shared
/// with plang_str.cpp's identical use of it.
[[noreturn]] void plang_err_field_width(int64_t W);

// plang_str_write_w's own comment (plang_str.cpp) explains why an unchecked
// width would pace an unbounded number of one-character writes (issue #247);
// the same guard, duplicated rather than shared, since it is `static`
// (internal linkage) there.
static int checkedWidth(int64_t w) {
    if (w > INT32_MAX) plang_err_field_width(w);
    return static_cast<int>(w);
}

// ---- I/O -------------------------------------------------------------------

void plang_sstr_write(const void* s, int64_t /*cap*/) {
    const uint8_t len = sstrLen(s);
    if (len > 0) plangOutN(sstrData(s), len);
}

void plang_sstr_writeln(const void* s, int64_t cap) {
    plang_sstr_write(s, cap);
    plangOutCh('\n');
}

/// Field-width write (ISO §6.10.3.6's rule, the same one plang_str_write_w
/// applies): the field is exactly w characters wide, so a longer value is
/// truncated and w == 0 writes nothing.  A negative w writes the value in
/// full, as if no width had been given, matching plang_str_write_w's own
/// (field-tested-against-FPC) reading of what a negative width means.
void plang_sstr_write_w(const void* s, int64_t /*cap*/, int64_t w) {
    if (w == 0) return;
    int64_t len = sstrLen(s);
    if (w < 0) { if (len > 0) plangOutN(sstrData(s), static_cast<size_t>(len)); return; }
    checkedWidth(w);
    for (int64_t i = 0, pad = w - len; i < pad; ++i) plangOutCh(' ');
    if (len > w) len = w;
    if (len > 0) plangOutN(sstrData(s), static_cast<size_t>(len));
}

void plang_sstr_writeln_w(const void* s, int64_t cap, int64_t w) {
    plang_sstr_write_w(s, cap, w);
    plangOutCh('\n');
}

/// Fills s with characters up to (not including) the line terminator.  Excess
/// input beyond the (255-clamped) capacity is discarded -- the read-side twin
/// of the write-side clamp above, and the same "no memory-unsafe overflow,
/// exact truncation semantics left to a later item" trade this file makes
/// throughout.  The terminator is left in the stream, matching
/// plang_str_read's own convention, so a following read sees eoln.
void plang_sstr_read(void* s, int64_t cap) {
    const int64_t ecap = effCap(cap);
    char*   data = sstrData(s);
    int64_t len  = 0;
    int c;
    while ((c = plangInCh()) != EOF && c != '\n')
        if (len < ecap) data[len++] = static_cast<char>(c);
    if (c == '\n') plangInUnget(c);
    sstrLen(s) = static_cast<uint8_t>(len);
}

} // extern "C"

} // namespace plang
