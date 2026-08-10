/// plang_io.cpp — Pascal I/O runtime (C++23)
///
/// Implements write / writeln / read for every scalar type the compiler can
/// produce.  Functions use the LLVM value type of their argument as a suffix
/// so codegen can select the right one purely from the IR type of the value.
///
/// All stdin/stdout traffic goes through the primitives in plang_stream.h so
/// that readstr and writestr can redirect it to a string; see that header.
///
/// All symbols are exported with C linkage so generated LLVM IR can call them
/// by name without name-mangling.

#include "plang_stream.h"
#include "plang_real.h"

#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plang {

// ---- redirect state (declared in plang_stream.h) ----

const char* PlangInBuf = nullptr;
std::size_t PlangInLen = 0;
std::size_t PlangInPos = 0;

namespace {

/// writestr's destination.  Null while output is going to stdout.  Grown on
/// demand and kept between statements so the common case does not re-allocate.
char*       CapBuf = nullptr;
std::size_t CapLen = 0;
std::size_t CapCap = 0;
bool        Capturing = false;

void capReserve(std::size_t Need) {
    if (Need <= CapCap) return;
    std::size_t NewCap = CapCap ? CapCap : 256;
    while (NewCap < Need) NewCap *= 2;
    if (char* P = static_cast<char*>(std::realloc(CapBuf, NewCap))) {
        CapBuf = P;
        CapCap = NewCap;
    }
}

} // namespace

void plangOutN(const char* Data, std::size_t N) {
    if (N == 0) return;
    if (!Capturing) { std::fwrite(Data, 1, N, stdout); return; }
    capReserve(CapLen + N);
    if (CapLen + N > CapCap) return;   // allocation failed; drop the excess
    std::memcpy(CapBuf + CapLen, Data, N);
    CapLen += N;
}

void plangOutStr(const char* S) {
    if (S) plangOutN(S, std::strlen(S));
}

void plangOutFmt(const char* Fmt, ...) {
    char    Buf[512];
    va_list Ap;
    va_start(Ap, Fmt);
    const int N = std::vsnprintf(Buf, sizeof Buf, Fmt, Ap);
    va_end(Ap);
    if (N < 0) return;
    if (static_cast<std::size_t>(N) < sizeof Buf) {
        plangOutN(Buf, static_cast<std::size_t>(N));
        return;
    }
    // Field widths are user-controlled, so fall back to an exact-size buffer.
    char* Big = static_cast<char*>(std::malloc(static_cast<std::size_t>(N) + 1));
    if (!Big) { plangOutN(Buf, sizeof Buf - 1); return; }
    va_start(Ap, Fmt);
    std::vsnprintf(Big, static_cast<std::size_t>(N) + 1, Fmt, Ap);
    va_end(Ap);
    plangOutN(Big, static_cast<std::size_t>(N));
    std::free(Big);
}

namespace {

/// ISO §6.9.1: reading a number skips preceding spaces and line terminators.
int skipBlanks() {
    int C;
    while ((C = plangInCh()) != EOF
           && (C == ' ' || C == '\t' || C == '\n' || C == '\r'))
    {}
    return C;
}

/// Collects the longest prefix that can form a number into \p Tok, which must
/// hold TokMax characters.  Digits only when \p Real is false; otherwise also
/// a fractional part and an exponent.
constexpr std::size_t TokMax = 64;

void scanNumber(char* Tok, bool Real) {
    std::size_t N = 0;
    auto put = [&](int C) { if (N + 1 < TokMax) Tok[N++] = static_cast<char>(C); };
    auto digits = [&](int& C) {
        while (C != EOF && std::isdigit(static_cast<unsigned char>(C)))
            { put(C); C = plangInCh(); }
    };

    int C = skipBlanks();
    if (C == '+' || C == '-') { put(C); C = plangInCh(); }
    digits(C);
    if (Real) {
        if (C == '.') { put(C); C = plangInCh(); digits(C); }
        if (C == 'e' || C == 'E') {
            put('e');
            C = plangInCh();
            if (C == '+' || C == '-') { put(C); C = plangInCh(); }
            digits(C);
        }
    }
    plangInUnget(C);
    Tok[N] = '\0';
}

void consumeLine() {
    int C;
    while ((C = plangInCh()) != EOF && C != '\n') {}
}

} // namespace

extern "C" {

// The width-taking real writers are defined further down with the rest of the
// field-width forms; the ones without a width are those with the default.
void plang_write_f64_e(double V, int64_t W);

// ---- write (no trailing newline) ----

void plang_write_i64 (int64_t     V) { plangOutFmt("%" PRId64, V); }
void plang_write_f64 (double      V) { plang_write_f64_e(V, PlangRealWidth); }
void plang_write_bool(int8_t      V) { plangOutStr(V ? "true" : "false"); }
void plang_write_char(int8_t      V) { plangOutCh(static_cast<unsigned char>(V)); }
void plang_write_str (const char *S) { plangOutStr(S ? S : ""); }

// ---- writeln (with trailing newline) ----

void plang_writeln_i64 (int64_t     V) { plangOutFmt("%" PRId64, V); plangOutCh('\n'); }
void plang_writeln_f64 (double      V) { plang_write_f64(V);         plangOutCh('\n'); }
void plang_writeln_bool(int8_t      V) { plangOutStr(V ? "true" : "false"); plangOutCh('\n'); }
void plang_writeln_char(int8_t      V) { plangOutCh(static_cast<unsigned char>(V)); plangOutCh('\n'); }
void plang_writeln_str (const char *S) { plangOutStr(S ? S : ""); plangOutCh('\n'); }
void plang_writeln     ()              { plangOutCh('\n'); }

// ---- EP §6.9.3.6: a complex value is written as a parenthesised real pair ----

void plang_write_cplx  (double Re, double Im) {
    plangOutCh('(');
    plang_write_f64(Re);
    plangOutCh(',');
    plang_write_f64(Im);
    plangOutCh(')');
}
void plang_writeln_cplx(double Re, double Im)
    { plang_write_cplx(Re, Im); plangOutCh('\n'); }
void plang_write_cplx_w(double Re, double Im, int64_t W, int64_t D) {
    // The width applies to each component, as it does for the two reals the
    // pair is written from — and so does the representation, which is why this
    // goes through the real writers rather than formatting the pair itself.
    plangOutCh('(');
    if (D >= 0) plangOutFmt("%*.*f", static_cast<int>(W), static_cast<int>(D), Re);
    else        plang_write_f64_e(Re, W);
    plangOutCh(',');
    if (D >= 0) plangOutFmt("%*.*f", static_cast<int>(W), static_cast<int>(D), Im);
    else        plang_write_f64_e(Im, W);
    plangOutCh(')');
}
void plang_writeln_cplx_w(double Re, double Im, int64_t W, int64_t D)
    { plang_write_cplx_w(Re, Im, W, D); plangOutCh('\n'); }

// ---- read ----

void plang_read_i64 (int64_t *P) {
    char Tok[TokMax];
    scanNumber(Tok, /*Real=*/false);
    if (Tok[0]) *P = std::strtoll(Tok, nullptr, 10);
}
void plang_read_f64 (double  *P) {
    char Tok[TokMax];
    scanNumber(Tok, /*Real=*/true);
    if (Tok[0]) *P = std::strtod(Tok, nullptr);
}
void plang_read_char(int8_t  *P) {
    const int C = plangInCh();
    *P = (C == EOF) ? 0 : static_cast<int8_t>(C);
}

// ---- readln ----

void plang_readln()                { consumeLine(); }
void plang_readln_i64 (int64_t *P) { plang_read_i64(P);  consumeLine(); }
void plang_readln_f64 (double  *P) { plang_read_f64(P);  consumeLine(); }
void plang_readln_char(int8_t  *P) { plang_read_char(P); consumeLine(); }

// ---- eof / eoln on the current input ----

int8_t plang_eof_stdin() {
    const int C = plangInCh();
    if (C == EOF) return 1;
    plangInUnget(C);
    return 0;
}

int8_t plang_eoln_stdin() {
    const int C = plangInCh();
    if (C == EOF) return 1;
    plangInUnget(C);
    return (C == '\n') ? 1 : 0;
}

// ---- page ----

void plang_page() { plangOutCh('\f'); }

// ---- write with field-width (ISO §6.9.3 / EP §6.10.3.1) ----
// EP §6.10.3.1(u) permits a TotalWidth of zero, but what that means depends on
// the type.  For char (§6.10.3.2), string (§6.10.3.6) and Boolean (§6.10.3.5,
// defined in terms of string) the field holds exactly TotalWidth characters, so
// zero writes nothing.  For integer (§6.10.3.3 case b) and real (§6.10.3.4.2)
// TotalWidth is a minimum: the value is always written, simply without padding,
// which is what a printf width of zero already does.

void plang_write_i64_w (int64_t V, int64_t W) { plangOutFmt("%*" PRId64, static_cast<int>(W), V); }
void plang_write_f64_e (double  V, int64_t W) {
    char Buf[PlangRealMaxChars];
    plangOutN(Buf, plangFormatReal(Buf, V, W));
}
void plang_write_f64_f (double  V, int64_t W, int64_t D)
    { plangOutFmt("%*.*f", static_cast<int>(W), static_cast<int>(D), V); }
// §6.9.3.6: the field is exactly TotalWidth characters wide, so a string
// longer than the field loses its tail rather than widening it — the `%*s` a
// field width otherwise maps onto pads but never truncates.
static void plangOutPadded(const char* S, int64_t W) {
    if (W <= 0) return;
    const auto Width = static_cast<size_t>(W);
    const size_t Len = S ? std::strlen(S) : 0;
    for (size_t I = Len; I < Width; ++I) plangOutCh(' ');
    if (Len) plangOutN(S, Len < Width ? Len : Width);
}

// §6.9.3.5 writes a boolean as the char-string 'true' or 'false' would be
// written, which is why it truncates too.
void plang_write_bool_w(int8_t      V, int64_t W) { plangOutPadded(V ? "true" : "false", W); }
void plang_write_char_w(int8_t      V, int64_t W) { if (W != 0) plangOutFmt("%*c", static_cast<int>(W), static_cast<unsigned char>(V)); }
void plang_write_str_w (const char *S, int64_t W) { plangOutPadded(S, W); }

// ---- writeln with field-width ----

void plang_writeln_i64_w (int64_t V, int64_t W) { plang_write_i64_w(V, W); plangOutCh('\n'); }
void plang_writeln_f64_e (double  V, int64_t W) { plang_write_f64_e(V, W); plangOutCh('\n'); }
void plang_writeln_f64_f (double  V, int64_t W, int64_t D)
    { plang_write_f64_f(V, W, D); plangOutCh('\n'); }
void plang_writeln_bool_w(int8_t      V, int64_t W) { plang_write_bool_w(V, W); plangOutCh('\n'); }
void plang_writeln_char_w(int8_t      V, int64_t W) { plang_write_char_w(V, W); plangOutCh('\n'); }
void plang_writeln_str_w (const char *S, int64_t W) { plang_write_str_w(S, W); plangOutCh('\n'); }

// ---- EP §6.7.5.5 string transfer procedures ----
//
// Codegen brackets the ordinary write/read argument lowering with these, so
// writestr and readstr inherit the full set of formats and parsing rules.

void plang_writestr_begin() {
    CapLen    = 0;
    Capturing = true;
}

/// Ends capture, storing the formatted text into the string(N) at \p S.
/// Characters beyond \p Cap are dropped, matching truncating assignment.
void plang_writestr_end(void *S, int64_t Cap) {
    Capturing = false;
    if (!S) return;
    auto Len = static_cast<int64_t>(CapLen);
    if (Len > Cap) Len = Cap;
    // string(N) is { i64 length, [N x i8] data }; see strStructType in codegen.
    auto* Base = static_cast<char*>(S);
    *reinterpret_cast<int64_t*>(Base) = Len;
    if (Len > 0)
        std::memcpy(Base + sizeof(int64_t), CapBuf, static_cast<std::size_t>(Len));
}

void plang_readstr_begin(const void *S, int64_t Len) {
    PlangInBuf = static_cast<const char*>(S);
    PlangInLen = (Len > 0) ? static_cast<std::size_t>(Len) : 0;
    PlangInPos = 0;
}

void plang_readstr_end() {
    PlangInBuf = nullptr;
    PlangInLen = 0;
    PlangInPos = 0;
}

} // extern "C"

} // namespace plang
