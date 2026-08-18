/// plang_str.cpp — EP string(N) runtime (C++23, ISO 10206 §6.4.3.3)
///
/// Layout of a string(N) variable in memory:
///   bytes  0–7 : int64_t  length   — current number of characters (0..N)
///   bytes  8..  : char     data[N]  — character data (NOT null-terminated)
///
/// Every runtime function receives a (ptr, capacity) pair so a single set of
/// functions can serve all string(N) types regardless of N.

#include "plang_stream.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace plang {

namespace {
inline int64_t& strLen(void* s)           { return *reinterpret_cast<int64_t*>(s); }
inline int64_t  strLen(const void* s)     { return *reinterpret_cast<const int64_t*>(s); }
inline char*    strData(void* s)          { return reinterpret_cast<char*>(s) + 8; }
inline const char* strData(const void* s) { return reinterpret_cast<const char*>(s) + 8; }
} // namespace

extern "C" {

/// Defined with the other runtime error reporters in plang_sys.cpp.
[[noreturn]] void plang_err_substr(int64_t I, int64_t N, int64_t Len);
[[noreturn]] void plang_err_substr_assign(int64_t Len, int64_t N);
[[noreturn]] void plang_err_str_capacity(int64_t Len, int64_t Cap);

// ---- initialization --------------------------------------------------------

void plang_str_init(void* s, int64_t cap) {
    strLen(s) = 0;
    std::memset(strData(s), 0, static_cast<size_t>(cap));
}

// ---- assignment ------------------------------------------------------------

// EP §6.9.2.2 makes it an error for the value assigned to exceed the capacity
// of the variable.  Truncating instead leaves a program computing on a string
// that is quietly not the one it wrote, which is the failure the length field
// exists to prevent.

void plang_str_assign(void* dst, int64_t cap_dst,
                      const void* src, int64_t /*cap_src*/) {
    int64_t len = strLen(src);
    if (len > cap_dst) plang_err_str_capacity(len, cap_dst);
    strLen(dst) = len;
    std::memcpy(strData(dst), strData(src), static_cast<size_t>(len));
}

void plang_str_from_cstr(void* dst, int64_t cap, const char* src) {
    if (!src) { strLen(dst) = 0; return; }
    int64_t len = static_cast<int64_t>(std::strlen(src));
    if (len > cap) plang_err_str_capacity(len, cap);
    strLen(dst) = len;
    std::memcpy(strData(dst), src, static_cast<size_t>(len));
}

void plang_str_from_char(void* dst, int64_t cap, int8_t c) {
    if (cap < 1) { strLen(dst) = 0; return; }
    strLen(dst)     = 1;
    strData(dst)[0] = static_cast<char>(c);
}

// ---- concatenation ---------------------------------------------------------

void plang_str_concat(void* dst, int64_t cap_dst,
                      const void* a, int64_t /*cap_a*/,
                      const void* b, int64_t /*cap_b*/) {
    int64_t la  = strLen(a), lb = strLen(b);
    int64_t ld  = std::min(la + lb, cap_dst);
    int64_t la2 = std::min(la, ld);
    int64_t lb2 = std::min(lb, ld - la2);
    std::memcpy(strData(dst),       strData(a), static_cast<size_t>(la2));
    std::memcpy(strData(dst) + la2, strData(b), static_cast<size_t>(lb2));
    strLen(dst) = la2 + lb2;
}

void plang_str_concat_cstr(void* dst, int64_t cap_dst,
                            const void* a, int64_t /*cap_a*/,
                            const char* cstr) {
    int64_t la  = strLen(a);
    int64_t lb  = cstr ? static_cast<int64_t>(std::strlen(cstr)) : 0;
    int64_t ld  = std::min(la + lb, cap_dst);
    int64_t la2 = std::min(la, ld);
    int64_t lb2 = std::min(lb, ld - la2);
    std::memcpy(strData(dst), strData(a), static_cast<size_t>(la2));
    if (lb2 > 0) std::memcpy(strData(dst) + la2, cstr, static_cast<size_t>(lb2));
    strLen(dst) = la2 + lb2;
}

void plang_str_concat_char(void* dst, int64_t cap_dst,
                            const void* a, int64_t /*cap_a*/,
                            int8_t c) {
    int64_t la  = strLen(a);
    int64_t ld  = std::min(la + 1, cap_dst);
    int64_t la2 = std::min(la, ld);
    std::memcpy(strData(dst), strData(a), static_cast<size_t>(la2));
    if (la2 < ld) strData(dst)[la2] = static_cast<char>(c);
    strLen(dst) = la2 + (la2 < ld ? 1 : 0);
}

// ---- comparison ------------------------------------------------------------

static int strCmp(const void* a, int64_t /*cap_a*/,
                  const void* b, int64_t /*cap_b*/) {
    int64_t la = strLen(a), lb = strLen(b);
    int64_t lm = std::max(la, lb);
    for (int64_t i = 0; i < lm; ++i) {
        char ca = i < la ? strData(a)[i] : ' ';
        char cb = i < lb ? strData(b)[i] : ' ';
        if (ca != cb) return (unsigned char)ca < (unsigned char)cb ? -1 : 1;
    }
    return 0;
}

int8_t plang_str_eq(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb) == 0; }
int8_t plang_str_ne(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb) != 0; }
int8_t plang_str_lt(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb)  < 0; }
int8_t plang_str_le(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb) <= 0; }
int8_t plang_str_gt(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb)  > 0; }
int8_t plang_str_ge(const void* a, int64_t ca, const void* b, int64_t cb) { return strCmp(a,ca,b,cb) >= 0; }

// ---- string functions ------------------------------------------------------

int64_t plang_str_length(const void* s, int64_t /*cap*/) { return strLen(s); }

int64_t plang_str_index(const void* s, int64_t /*cap_s*/,
                         const void* pat, int64_t /*cap_pat*/) {
    int64_t ls = strLen(s), lp = strLen(pat);
    if (lp == 0) return 1;
    if (lp > ls) return 0;
    const char* sd = strData(s);
    const char* pd = strData(pat);
    for (int64_t i = 0; i <= ls - lp; ++i)
        if (std::memcmp(sd + i, pd, static_cast<size_t>(lp)) == 0)
            return i + 1;
    return 0;
}

/// EP §6.7.5.4: the \p n characters of \p src starting at index \p i.  The
/// third argument is a length, not an end index; the two agree only when i is
/// 1, which is why the difference is easy to miss.
void plang_str_substr(void* dst, int64_t cap_dst,
                       const void* src, int64_t /*cap_src*/,
                       int64_t i, int64_t n) {
    const int64_t ls = strLen(src);
    if (n < 0 || i < 1 || (n > 0 && i + n - 1 > ls)) plang_err_substr(i, n, ls);
    const int64_t len = std::min(n, cap_dst);
    std::memcpy(strData(dst), strData(src) + (i - 1), static_cast<size_t>(len));
    strLen(dst) = len;
}

/// EP §6.5.6: a substring is a variable, so it can be assigned to.  Its type
/// is a fixed string of exactly \p n characters, and the value assigned has to
/// be that long — a shorter one would leave part of the substring undefined,
/// which a fixed string has no way to say.
///
/// \p n arrives as \c high-low+1 (the caller has already collapsed the two
/// index-expressions to one length), so "the first index-expression is
/// greater than the second" -- 6.5.6's own wording for what a
/// substring-variable's indices must never do -- is exactly \c n<=0, not
/// \c n<0: low>high means high-low is negative, i.e. n=high-low+1<=0, and
/// n=0 is reached only when low is exactly one past high (there is no other
/// way to make high-low+1 come out to zero).  Before, only n<0 was checked,
/// so s[j+1..j] := '' silently copied zero bytes instead of being reported --
/// the one index relationship this clause names was left partly unchecked.
void plang_str_substr_assign(void* dst, int64_t /*cap_dst*/,
                              int64_t i, int64_t n,
                              const void* src, int64_t /*cap_src*/) {
    const int64_t ld = strLen(dst);
    if (n <= 0 || i < 1 || i + n - 1 > ld) plang_err_substr(i, n, ld);
    const int64_t ls = strLen(src);
    if (ls != n) plang_err_substr_assign(ls, n);
    std::memcpy(strData(dst) + (i - 1), strData(src), static_cast<size_t>(n));
}

void plang_str_trim(void* dst, int64_t cap_dst,
                     const void* src, int64_t /*cap_src*/) {
    int64_t ls = strLen(src);
    while (ls > 0 && strData(src)[ls - 1] == ' ') --ls;
    int64_t len = std::min(ls, cap_dst);
    std::memcpy(strData(dst), strData(src), static_cast<size_t>(len));
    strLen(dst) = len;
}

// ---- I/O -------------------------------------------------------------------

void plang_str_write(const void* s, int64_t /*cap*/) {
    int64_t len = strLen(s);
    if (len > 0) plangOutN(strData(s), static_cast<size_t>(len));
}

void plang_str_writeln(const void* s, int64_t cap) {
    plang_str_write(s, cap);
    plangOutCh('\n');
}

/// ISO §6.10.3.6: the field is exactly w characters, so a longer string is
/// truncated and w = 0 writes nothing.
void plang_str_write_w(const void* s, int64_t /*cap*/, int64_t w) {
    if (w == 0) return;
    int64_t len = strLen(s);
    for (int64_t i = 0, pad = w - len; i < pad; ++i) plangOutCh(' ');
    if (len > w) len = w;
    if (len > 0) plangOutN(strData(s), static_cast<size_t>(len));
}

void plang_str_writeln_w(const void* s, int64_t cap, int64_t w) {
    plang_str_write_w(s, cap, w);
    plangOutCh('\n');
}

/// Fills s with characters up to (not including) the line terminator.  Excess
/// input beyond cap is discarded, matching the truncating assignment rule.
/// The terminator is left in the stream so a following read sees eoln.
void plang_str_read(void* s, int64_t cap) {
    char*   data = strData(s);
    int64_t len  = 0;
    int c;
    while ((c = plangInCh()) != EOF && c != '\n')
        if (len < cap) data[len++] = static_cast<char>(c);
    if (c == '\n') plangInUnget(c);
    strLen(s) = len;
}

/// ISO §6.10.1(e): a fixed-string-type of capacity n reads up to the line
/// terminator, same as plang_str_read, but has no length field to record how
/// much of it was real -- so the components past what was read take the
/// value the standard requires, "zero or more spaces", rather than whatever
/// the buffer already held.
void plang_str_read_fixed(void* buf, int64_t n) {
    char*   data = static_cast<char*>(buf);
    int64_t len  = 0;
    int c;
    while ((c = plangInCh()) != EOF && c != '\n') {
        if (len < n) data[len] = static_cast<char>(c);
        ++len;
    }
    if (c == '\n') plangInUnget(c);
    for (int64_t i = len; i < n; ++i) data[i] = ' ';
}

} // extern "C"

} // namespace plang
