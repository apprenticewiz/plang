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
/// SCOPE: this file originally gave ShortString's TYPE/LAYOUT existence the
/// minimal runtime support a bare declaration and a basic write/read
/// round-trip needed to not crash.  It now ALSO implements Turbo Pascal's
/// actual string[N] VALUE semantics: truncating assignment/from-literal
/// (plang_sstr_assign and friends -- truncates rather than erroring, unlike
/// EP's plang_str_assign in plang_str.cpp), prefix lexicographic comparison
/// with shorter-is-less (plang_sstr_eq and friends -- the OPPOSITE of EP's
/// space-padded plang_str_eq family: 'a' < 'a ' is true here, false there),
/// and clamped-at-capacity concatenation (plang_sstr_concat and friends).
/// It still does NOT implement s[0]-as-length-byte (that needs no runtime
/// entry point at all -- see CGIndexAccess.cpp, which addresses it directly
/// as an ordinary byte of the struct).
///
/// The System-unit string routines (a later, now-landed work item) add
/// Copy/Pos/Delete/Insert/SetLength/StringOfChar below, following this
/// file's own established conventions throughout (truncate/clamp rather
/// than raise, a (ptr, capacity) pair per string argument even where the
/// capacity is not read).  Concat needs no runtime entry point of its own --
/// CGFuncCall.cpp builds it by chaining plang_sstr_concat, just above,
/// starting from an empty accumulator.  UpCase needs none either (a single
/// i8 branch, kept inline in CGFuncCall.cpp).  Str/Val are a different
/// shape entirely -- Str reuses the writestr capture machinery
/// (plang_writestr_end_sstr, plang_io.cpp) and Val is a non-fatal parser
/// with no truncate-and-continue shape to share with anything here
/// (runtime/plang_val.cpp) -- so neither lives in this file.

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

// ---- assignment -------------------------------------------------------------

// Turbo string[N] has no EP-style capacity ERROR (ISO 10206 §6.9.2.2 is an
// Extended Pascal rule, never Turbo's): a value longer than the destination's
// capacity silently TRUNCATES, matching real Turbo/FPC field practice.  Every
// function below clamps to effCap(cap_dst) rather than calling any
// plang_err_str_capacity-style reporter -- contrast with plang_str_assign
// (plang_str.cpp), this file's EP sibling, which errors instead.

void plang_sstr_assign(void* dst, int64_t cap_dst,
                        const void* src, int64_t /*cap_src*/) {
    const int64_t ecap = effCap(cap_dst);
    int64_t len = sstrLen(src);
    if (len > ecap) len = ecap;
    sstrLen(dst) = static_cast<uint8_t>(len);
    std::memcpy(sstrData(dst), sstrData(src), static_cast<size_t>(len));
}

// Length-aware source (a string literal, materialized with its compile-time
// byte count) -- the ShortString sibling of plang_str_from_bytes.  ISO
// 7185 §6.1.7/EP §6.1.8 place no restriction on what characters appear
// between a literal's quotes (a literal may contain NUL like any other
// character), so this takes an explicit length rather than scanning for a
// terminator the way plang_sstr_from_cstr below has to.
void plang_sstr_from_bytes(void* dst, int64_t cap, const char* src, int64_t len) {
    const int64_t ecap = effCap(cap);
    if (!src || len <= 0) { sstrLen(dst) = 0; return; }
    if (len > ecap) len = ecap;
    sstrLen(dst) = static_cast<uint8_t>(len);
    std::memcpy(sstrData(dst), src, static_cast<size_t>(len));
}

void plang_sstr_from_cstr(void* dst, int64_t cap, const char* src) {
    if (!src) { sstrLen(dst) = 0; return; }
    plang_sstr_from_bytes(dst, cap, src, static_cast<int64_t>(std::strlen(src)));
}

void plang_sstr_from_char(void* dst, int64_t cap, int8_t c) {
    if (effCap(cap) < 1) { sstrLen(dst) = 0; return; }
    sstrLen(dst)     = 1;
    sstrData(dst)[0] = static_cast<char>(c);
}

// ---- concatenation ------------------------------------------------------

// Mirrors plang_str_concat's own std::min(len, cap_dst) clamp (plang_str.cpp)
// exactly -- a ShortString's declared capacity can never usefully exceed 255
// (effCap), so the same shape serves both without any extra logic of its own.

void plang_sstr_concat(void* dst, int64_t cap_dst,
                        const void* a, int64_t /*cap_a*/,
                        const void* b, int64_t /*cap_b*/) {
    const int64_t ecap = effCap(cap_dst);
    int64_t la  = sstrLen(a), lb = sstrLen(b);
    int64_t ld  = std::min(la + lb, ecap);
    int64_t la2 = std::min(la, ld);
    int64_t lb2 = std::min(lb, ld - la2);
    std::memcpy(sstrData(dst),       sstrData(a), static_cast<size_t>(la2));
    std::memcpy(sstrData(dst) + la2, sstrData(b), static_cast<size_t>(lb2));
    sstrLen(dst) = static_cast<uint8_t>(la2 + lb2);
}

void plang_sstr_concat_cstr(void* dst, int64_t cap_dst,
                             const void* a, int64_t /*cap_a*/,
                             const char* cstr) {
    const int64_t ecap = effCap(cap_dst);
    int64_t la  = sstrLen(a);
    int64_t lb  = cstr ? static_cast<int64_t>(std::strlen(cstr)) : 0;
    int64_t ld  = std::min(la + lb, ecap);
    int64_t la2 = std::min(la, ld);
    int64_t lb2 = std::min(lb, ld - la2);
    std::memcpy(sstrData(dst), sstrData(a), static_cast<size_t>(la2));
    if (lb2 > 0) std::memcpy(sstrData(dst) + la2, cstr, static_cast<size_t>(lb2));
    sstrLen(dst) = static_cast<uint8_t>(la2 + lb2);
}

void plang_sstr_concat_char(void* dst, int64_t cap_dst,
                             const void* a, int64_t /*cap_a*/,
                             int8_t c) {
    const int64_t ecap = effCap(cap_dst);
    int64_t la  = sstrLen(a);
    int64_t ld  = std::min(la + 1, ecap);
    int64_t la2 = std::min(la, ld);
    std::memcpy(sstrData(dst), sstrData(a), static_cast<size_t>(la2));
    if (la2 < ld) sstrData(dst)[la2] = static_cast<char>(c);
    sstrLen(dst) = static_cast<uint8_t>(la2 + (la2 < ld ? 1 : 0));
}

// ---- System-unit string routines -------------------------------------------
//
// Copy/Pos/Delete/Insert/SetLength/StringOfChar -- see this file's own
// header comment for what does NOT live here (Concat, UpCase, Str, Val) and
// why.  Every empirically-derived rule below was checked against a local
// `fpc -Mtp` install; see the PR this shipped in for the full transcript.

/// Copy(s, index, count) -- CLAMPS index/count into range rather than
/// raising the way EP's plang_str_substr does for an out-of-range request.
/// index < 1 clamps to 1 (the count is NOT re-based off the clamp: `Copy(s,
/// 0, 5)` and `Copy(s, 1, 5)` give the same five characters, confirmed
/// against `fpc -Mtp`); count < 0 clamps to 0; an index beyond the source's
/// own length yields an empty result.
void plang_sstr_copy(void* dst, int64_t cap_dst,
                      const void* src, int64_t /*cap_src*/,
                      int64_t index, int64_t count) {
    const int64_t ecap = effCap(cap_dst);
    const int64_t srcLen = sstrLen(src);
    int64_t idx = index;
    if (idx < 1) idx = 1;
    int64_t cnt = count;
    if (cnt < 0) cnt = 0;
    int64_t avail = 0;
    if (idx <= srcLen) avail = srcLen - idx + 1;
    if (cnt > avail) cnt = avail;
    if (cnt > ecap) cnt = ecap;
    if (cnt > 0) std::memcpy(sstrData(dst), sstrData(src) + (idx - 1), static_cast<size_t>(cnt));
    sstrLen(dst) = static_cast<uint8_t>(cnt);
}

/// Pos(pat, s) -- 1-based index of the first match, 0 if none.  An EMPTY
/// pattern is 0 -- confirmed against `fpc -Mtp`, the OPPOSITE of EP's own
/// plang_str_index('', s) = 1 (ISO 10206's own rule for `index`); this is a
/// wholly separate function from plang_str_index for exactly that reason,
/// even though the search itself is the same naive substring scan.
int64_t plang_sstr_pos(const void* pat, int64_t /*cap_pat*/,
                        const void* s, int64_t /*cap_s*/) {
    const int64_t plen = sstrLen(pat);
    const int64_t slen = sstrLen(s);
    if (plen == 0 || plen > slen) return 0;
    const char* pd = sstrData(pat);
    const char* sd = sstrData(s);
    for (int64_t i = 0; i <= slen - plen; ++i)
        if (std::memcmp(sd + i, pd, static_cast<size_t>(plen)) == 0) return i + 1;
    return 0;
}

/// Delete(var s, index, count) -- removes count characters starting at
/// index, MUTATING s in place.  Unlike Copy, an out-of-range index (< 1 or >
/// Length(s)) makes the whole call a NO-OP rather than clamping -- confirmed
/// against `fpc -Mtp`: Delete(s, 0, 2) and Delete(s, 100, 3) both leave s
/// completely unchanged.  count is still clamped to what is actually
/// available (and to >= 0), matching Copy's own count rule.
void plang_sstr_delete(void* s, int64_t /*cap_s*/, int64_t index, int64_t count) {
    const int64_t len = sstrLen(s);
    if (index < 1 || index > len) return;
    int64_t cnt = count;
    if (cnt < 0) cnt = 0;
    const int64_t avail = len - index + 1;
    if (cnt > avail) cnt = avail;
    if (cnt == 0) return;
    char* data = sstrData(s);
    const int64_t tailStart = index - 1 + cnt;
    const int64_t tailLen   = len - tailStart;
    if (tailLen > 0) std::memmove(data + (index - 1), data + tailStart, static_cast<size_t>(tailLen));
    sstrLen(s) = static_cast<uint8_t>(len - cnt);
}

/// Insert(source, var s, index) -- inserts source into s before position
/// index, MUTATING s in place, clamped at s's own declared capacity.  Unlike
/// Delete, an out-of-range index IS clamped (to 1, or to Length(s)+1) rather
/// than making the call a no-op -- confirmed against `fpc -Mtp`.  Built into
/// a 255-byte scratch buffer first (kMaxLen bounds it, so no dynamic
/// allocation is needed) rather than shifted in place, since the source and
/// the tail being displaced can overlap the destination in ways a single
/// memmove cannot express in one pass.
void plang_sstr_insert(void* s, int64_t cap_s, const void* src, int64_t /*cap_src*/,
                        int64_t index) {
    const int64_t ecap = effCap(cap_s);
    int64_t len = sstrLen(s);
    if (len > ecap) len = ecap;
    int64_t idx = index;
    if (idx < 1) idx = 1;
    if (idx > len + 1) idx = len + 1;
    const int64_t idx0 = idx - 1;
    const int64_t srcLen = sstrLen(src);
    const char* sdata = sstrData(s);
    const char* xdata = sstrData(src);
    char buf[kMaxLen];
    int64_t pos = 0;
    for (int64_t i = 0; i < idx0 && pos < ecap; ++i) buf[pos++] = sdata[i];
    for (int64_t i = 0; i < srcLen && pos < ecap; ++i) buf[pos++] = xdata[i];
    for (int64_t i = idx0; i < len && pos < ecap; ++i) buf[pos++] = sdata[i];
    std::memcpy(sstrData(s), buf, static_cast<size_t>(pos));
    sstrLen(s) = static_cast<uint8_t>(pos);
}

/// SetLength(var s, newLength) -- sets s's own length byte directly, clamped
/// to [0, s's declared capacity].  Bytes exposed by growing are left exactly
/// as they were (no zero-fill, no space-padding) -- confirmed against
/// `fpc -Mtp` that real Turbo/FPC does not touch them either.  Deliberately
/// does NOT reproduce two further fpc quirks also confirmed empirically:
/// real `fpc -Mtp` lets SetLength write a length byte past a narrow
/// string[N]'s own physical storage with no clamp at all (an actual buffer
/// overrun, `Runtime error 201` under `-Cr` for a later out-of-bounds
/// access), AND writes a NEGATIVE newLength through as a raw byte
/// reinterpretation rather than refusing or clamping it -- SetLength(s,
/// -1) left Length(s) = 255, i.e. real fpc's implementation is simply
/// `PByte(@s)^ := Byte(newLength)` with no range check in either direction.
/// See Builtins.def's own comment on SetLength for the full transcript.
/// plang clamps to [0, cap_dst] here the same way every other function in
/// this file already does -- both real quirks above are memory-safety or
/// input-validation holes, not "ambiguous field practice" worth matching.
void plang_sstr_setlength(void* s, int64_t cap_s, int64_t newLen) {
    const int64_t ecap = effCap(cap_s);
    int64_t n = newLen;
    if (n < 0) n = 0;
    if (n > ecap) n = ecap;
    sstrLen(s) = static_cast<uint8_t>(n);
}

/// StringOfChar(ch, count) -- count copies of ch, clamped at cap_dst (255 by
/// construction: Builtins.def's own comment on why StringOfChar's result is
/// always capacity-255).
void plang_sstr_of_char(void* dst, int64_t cap_dst, int8_t ch, int64_t count) {
    const int64_t ecap = effCap(cap_dst);
    int64_t n = count;
    if (n < 0) n = 0;
    if (n > ecap) n = ecap;
    if (n > 0) std::memset(sstrData(dst), ch, static_cast<size_t>(n));
    sstrLen(dst) = static_cast<uint8_t>(n);
}

// ---- comparison -----------------------------------------------------------

// Turbo string[N] compares as a PREFIX lexicographic order with SHORTER
// treated as LESS -- the OPPOSITE of EP's plang_str.cpp strCmp, which pads
// the shorter operand out to the longer one's length with spaces before
// comparing (so 'a' equals 'a ' there).  Neither that padding step nor any
// other part of strCmp is reused here: this compares only the overlapping
// PREFIX, and any leftover length alone (not a padding character) breaks the
// tie, matching real Turbo/FPC field practice ('a' < 'a ' is true).
static int sstrCmp(const void* a, int64_t /*cap_a*/,
                    const void* b, int64_t /*cap_b*/) {
    const int64_t la = sstrLen(a), lb = sstrLen(b);
    const int64_t lm = std::min(la, lb);
    for (int64_t i = 0; i < lm; ++i) {
        const auto ca = static_cast<unsigned char>(sstrData(a)[i]);
        const auto cb = static_cast<unsigned char>(sstrData(b)[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (la != lb) return la < lb ? -1 : 1;
    return 0;
}

int8_t plang_sstr_eq(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb) == 0; }
int8_t plang_sstr_ne(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb) != 0; }
int8_t plang_sstr_lt(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb)  < 0; }
int8_t plang_sstr_le(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb) <= 0; }
int8_t plang_sstr_gt(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb)  > 0; }
int8_t plang_sstr_ge(const void* a, int64_t ca, const void* b, int64_t cb) { return sstrCmp(a,ca,b,cb) >= 0; }

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
