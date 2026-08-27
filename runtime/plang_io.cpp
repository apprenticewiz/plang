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

/// One level of writestr's destination buffer.  Grown on demand and kept
/// between calls at the same nesting depth so the common (non-nested) case
/// does not re-allocate.
struct CapFrame {
    char*       Buf = nullptr;
    std::size_t Len = 0;
    std::size_t Cap = 0;
};

/// Stack of capture frames, one per writestr currently in progress.  A
/// writestr's write-parameters are ordinary expressions, so one of them may
/// call a function that itself calls writestr (e.g. `writestr(s, 'x', f())`
/// where f writes into a string of its own) -- capture state has to nest
/// rather than share a single buffer, or the inner call clobbers the outer
/// call's in-progress text, and the inner call's `Capturing = false` sends
/// the rest of the outer call's output to stdout instead of into its buffer.
/// Index CapDepth-1 is the innermost (currently-writing) frame; CapDepth ==
/// 0 means output goes to stdout.
CapFrame*   CapStack    = nullptr;
std::size_t CapStackCap = 0;   // allocated slots in CapStack
std::size_t CapDepth    = 0;   // active frames

/// Grows CapStack, if needed, to hold at least \p Need frames.
void capStackReserve(std::size_t Need) {
    if (Need <= CapStackCap) return;
    std::size_t NewCap = CapStackCap ? CapStackCap : 4;
    while (NewCap < Need) NewCap *= 2;
    auto* P = static_cast<CapFrame*>(std::realloc(CapStack, NewCap * sizeof(CapFrame)));
    if (!P) return;   // allocation failed; caller sees CapStackCap unchanged
    for (std::size_t I = CapStackCap; I < NewCap; ++I) {
        P[I].Buf = nullptr;
        P[I].Len = 0;
        P[I].Cap = 0;
    }
    CapStack    = P;
    CapStackCap = NewCap;
}

void capReserve(CapFrame& F, std::size_t Need) {
    if (Need <= F.Cap) return;
    std::size_t NewCap = F.Cap ? F.Cap : 256;
    while (NewCap < Need) NewCap *= 2;
    if (char* P = static_cast<char*>(std::realloc(F.Buf, NewCap))) {
        F.Buf = P;
        F.Cap = NewCap;
    }
}

} // namespace

void plangOutN(const char* Data, std::size_t N) {
    if (N == 0) return;
    if (CapDepth == 0) { std::fwrite(Data, 1, N, stdout); return; }
    CapFrame& F = CapStack[CapDepth - 1];
    capReserve(F, F.Len + N);
    if (F.Len + N > F.Cap) return;   // allocation failed; drop the excess
    std::memcpy(F.Buf + F.Len, Data, N);
    F.Len += N;
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
void plang_write_f64_f(double V, int64_t W, int64_t D);

/// Defined with the other runtime error reporters in plang_sys.cpp.
[[noreturn]] void plang_err_field_width(int64_t W);

// ISO §6.10.3.1 calls a negative TotalWidth or FracDigits "an error" (§3.2's
// weaker class, which a processor may leave undetected) rather than saying
// what it means.  Checked directly against FPC, in both its default and
// Turbo-compatibility modes: neither treats a negative width as the
// zero-width rule the field-width writers below give several types of their
// own (which would drop a string's or Boolean's whole value) -- it behaves
// as though no width had been written at all, uniformly across every type.
// Before this, `%*d`/`%*c`/`%*.*f` fed a negative width straight to printf,
// whose `*` takes a negative argument as its own left-justify flag with the
// field set to the value's absolute value -- an accident of libc, not a
// considered choice.
static int64_t noPadIfNegative(int64_t W) { return W < 0 ? 0 : W; }

// printf's `%*d`/`%*c` take the field width as a plain int, but a Pascal
// TotalWidth is int64_t -- a value computed at runtime can exceed INT32_MAX,
// and simply truncating it (the bug in issue #15) would silently reinterpret
// it as an unrelated, possibly huge width instead of catching the mistake.
static int checkedWidth(int64_t W) {
    W = noPadIfNegative(W);
    if (W > INT32_MAX) plang_err_field_width(W);
    return static_cast<int>(W);
}

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

// ---- EP §6.9.3.6: a complex value is written as a parenthesized real pair ----

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
    // plang_write_f64_f already picks between "%*.*f" and the exponential
    // fallback on D's sign, which used to be duplicated here inline.
    plangOutCh('(');
    plang_write_f64_f(Re, W, D);
    plangOutCh(',');
    plang_write_f64_f(Im, W, D);
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
//
// A NEGATIVE width is not the same as zero width -- see noPadIfNegative above
// -- so it must not fold into the zero-width case here, which for char/string/
// Boolean would drop the value's text outright rather than merely misjudging
// its padding.

void plang_write_i64_w (int64_t V, int64_t W) {
    plangOutFmt("%*" PRId64, checkedWidth(W), V);
}
void plang_write_f64_e (double  V, int64_t W) {
    char Buf[PlangRealMaxChars];
    plangOutN(Buf, plangFormatReal(Buf, V, W));
}
// A negative FracDigits falls back to the same exponential format omitting
// the decimals clause entirely produces, exactly as plang_write_cplx_w's own
// per-component formatting already did before it started calling this.
void plang_write_f64_f (double  V, int64_t W, int64_t D) {
    if (D < 0) { plang_write_f64_e(V, W); return; }
    plangOutFmt("%*.*f", checkedWidth(W), checkedWidth(D), V);
}
// §6.9.3.6: the field is exactly TotalWidth characters wide, so a string
// longer than the field loses its tail rather than widening it — the `%*s` a
// field width otherwise maps onto pads but never truncates.  A negative width
// truncates nothing and pads nothing: the value is written in full, as if no
// width had been given at all.
static void plangOutPadded(const char* S, int64_t W) {
    if (W == 0) return;
    const size_t Len = S ? std::strlen(S) : 0;
    if (W < 0) { if (Len) plangOutN(S, Len); return; }
    // W > 0 here: the numeric/char writers above hand W to printf's `%*d`/
    // `%*c`, so checkedWidth's INT32_MAX trap guards their width argument
    // from silently truncating (issue #15). This writer paces its own
    // padding loop by hand instead of going through printf, so without the
    // same call an oversized W (write(s:maxint)) just pads one character at
    // a time until it gets there -- no truncation to guard against, but an
    // unbounded amount of CPU and output for a value nothing could ever
    // read (issue #247).
    const auto Width = static_cast<size_t>(checkedWidth(W));
    for (size_t I = Len; I < Width; ++I) plangOutCh(' ');
    if (Len) plangOutN(S, Len < Width ? Len : Width);
}

// §6.9.3.5 writes a boolean as the char-string 'true' or 'false' would be
// written, which is why it truncates too.
void plang_write_bool_w(int8_t      V, int64_t W) { plangOutPadded(V ? "true" : "false", W); }
void plang_write_char_w(int8_t      V, int64_t W) {
    if (W == 0) return;
    if (W < 0) { plangOutCh(static_cast<unsigned char>(V)); return; }
    plangOutFmt("%*c", checkedWidth(W), static_cast<unsigned char>(V));
}
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
    capStackReserve(CapDepth + 1);
    if (CapDepth >= CapStackCap) return;   // allocation failed; degrade to a no-op frame
    CapStack[CapDepth].Len = 0;
    ++CapDepth;
}

/// Ends capture, storing the formatted text into the string(N) at \p S.
/// Characters beyond \p Cap are dropped, matching truncating assignment.
/// Pops this writestr's frame off CapStack so an enclosing writestr (if any)
/// resumes capturing into its own buffer instead of falling through to stdout.
void plang_writestr_end(void *S, int64_t Cap) {
    std::size_t Len = 0;
    const char* Buf = nullptr;
    if (CapDepth > 0) {
        CapFrame& F = CapStack[--CapDepth];
        Len = F.Len;
        Buf = F.Buf;
    }
    if (!S) return;
    auto L = static_cast<int64_t>(Len);
    if (L > Cap) L = Cap;
    // string(N) is { i64 length, [N x i8] data }; see strStructType in codegen.
    auto* Base = static_cast<char*>(S);
    *reinterpret_cast<int64_t*>(Base) = L;
    if (L > 0)
        std::memcpy(Base + sizeof(int64_t), Buf, static_cast<std::size_t>(L));
}

/// The fixed-string-type sibling: EP §6.7.5.5 defines writestr(s, ...) as
/// `read(f, ss)` on an auxiliary file holding the formatted text, and ISO
/// §6.10.1(e) is what a fixed-string read does -- no length field, so
/// whatever is short of capacity N is padded with spaces rather than left
/// out of a length count.
void plang_writestr_end_fixed(void *Buf, int64_t N) {
    std::size_t Len = 0;
    const char* Src = nullptr;
    if (CapDepth > 0) {
        CapFrame& F = CapStack[--CapDepth];
        Len = F.Len;
        Src = F.Buf;
    }
    if (!Buf) return;
    auto L = static_cast<int64_t>(Len);
    if (L > N) L = N;
    auto* Data = static_cast<char*>(Buf);
    if (L > 0) std::memcpy(Data, Src, static_cast<std::size_t>(L));
    for (int64_t I = L; I < N; ++I) Data[I] = ' ';
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
