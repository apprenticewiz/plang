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
#include <cerrno>
#include <cinttypes>
#include <cmath>
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

// ---- Crt cursor tracking (issue #704) --------------------------------------
//
// The Crt unit's WhereX/WhereY (share/plang/units/Crt.pas) model a software
// cursor rather than reading a live hardware/terminal one -- see that file's
// own header comment for why (real TP's own WhereX/WhereY are pure state
// reads too).  GotoXY/ClrScr already kept that model in sync for explicit
// cursor MOVES (via the CrtSyncCursor builtin, CGProcCall.cpp), but ordinary
// Write/Writeln output was never accounted for: writing text moved the
// terminal's REAL cursor forward without the model ever finding out.
// plangCrtTrackOutput (declared in plang_stream.h) is the fix: called from
// here (every unqualified Write/Writeln/WriteStr argument NOT itself being
// captured by writestr, see this file's own CapDepth just above) AND from
// plang_file.cpp's own turbo char/string file writers when their target is
// stdout (a plain `Write`/`Writeln` -- no explicit file argument -- is, under
// -std=turbo, ALWAYS lowered through the implicit Output file variable, see
// BuiltinIO.cpp's own turboStdFilePtr, so that second call site is not an
// optional extra, it is the one that actually fires for ordinary Crt
// programs).  Crt's own WhereX/WhereY (the CrtTrackedX/CrtTrackedY builtins,
// CGFuncCall.cpp) read the tracked result back.
//
// ANSI/VT100 escape sequences -- GotoXY's own cursor-move, ApplyAttr's own
// SGR color codes, ClrScr's own erase codes, all plain Pascal Writes from
// Crt.pas's own point of view -- must NOT be counted as ordinary text (a
// real terminal does not advance its cursor for the escape bytes
// themselves), so a small CSI (ESC '[' ... final-byte) state machine skips
// them entirely.  That state has to persist ACROSS calls, not just within
// one: one Pascal `Write(a, b, c)` lowers to one runtime call PER ARGUMENT,
// so GotoXY's own Write(Chr(27), '[', AbsY, ';', AbsX, 'H') arrives here as
// six separate calls, not one.
namespace {
enum class CrtEscState : uint8_t { Normal, SawEsc, InCsi };
CrtEscState CrtEsc        = CrtEscState::Normal;
int64_t     CrtCursorCol  = 1;  // 1-based, window-relative -- see Crt.pas
int64_t     CrtCursorRow  = 1;
} // namespace

void plangCrtTrackOutput(const char* Data, std::size_t N) {
    for (std::size_t I = 0; I < N; ++I) {
        const auto C = static_cast<unsigned char>(Data[I]);
        switch (CrtEsc) {
        case CrtEscState::Normal:
            if (C == 0x1B)      { CrtEsc = CrtEscState::SawEsc; }
            else if (C == '\n') { CrtCursorCol = 1; ++CrtCursorRow; }
            else if (C == '\r') { CrtCursorCol = 1; }
            else                { ++CrtCursorCol; }
            break;
        case CrtEscState::SawEsc:
            // Every escape Crt.pas itself ever writes is CSI ("ESC[...");
            // an unrecognized shape is simply abandoned rather than
            // mis-tracked as ordinary text.
            CrtEsc = (C == '[') ? CrtEscState::InCsi : CrtEscState::Normal;
            break;
        case CrtEscState::InCsi:
            // Parameter/intermediate bytes are 0x20-0x3F; the sequence's
            // own final byte is 0x40-0x7E (ECMA-48 §5.4) -- neither ever
            // advances the cursor on a real terminal.
            if (C >= 0x40 && C <= 0x7E) CrtEsc = CrtEscState::Normal;
            break;
        }
    }
}

extern "C" {
void plang_crt_sync_cursor(int64_t X, int64_t Y) {
    CrtCursorCol = X;
    CrtCursorRow = Y;
}
int64_t plang_crt_tracked_x() { return CrtCursorCol; }
int64_t plang_crt_tracked_y() { return CrtCursorRow; }
} // extern "C"

void plangOutN(const char* Data, std::size_t N) {
    if (N == 0) return;
    if (CapDepth == 0) {
        std::fwrite(Data, 1, N, stdout);
        plangCrtTrackOutput(Data, N);
        return;
    }
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

/// scanNumber's token buffer.  Issue #237: this used to be a fixed 64
/// characters, silently truncating any longer literal -- most visibly, a
/// long real's exponent digits fell off the end along with everything else
/// past the limit, so "1" followed by 71 more digits read back as 1e+62
/// instead of 1e71.  Grown on demand like writestr's CapBuf just above, and
/// kept between calls for the same reason: the common case allocates once.
char*       TokBuf = nullptr;
std::size_t TokCap = 0;

void tokReserve(std::size_t Need) {
    if (Need <= TokCap) return;
    std::size_t NewCap = TokCap ? TokCap : 64;
    while (NewCap < Need) NewCap *= 2;
    if (char* P = static_cast<char*>(std::realloc(TokBuf, NewCap))) {
        TokBuf = P;
        TokCap = NewCap;
    }
}

/// Collects the longest prefix that can form a number into TokBuf, growing
/// it rather than truncating (issue #237).  Digits only when \p Real is
/// false; otherwise also a fractional part and an exponent.
///
/// \p SawAny reports whether a non-blank character was available at all
/// before end-of-file -- how the caller tells a malformed token (issue
/// #236: something was there and it didn't parse) apart from a legitimate
/// read past the end of the input (issue #284: nothing was left to read).
void scanNumber(bool Real, bool &SawAny) {
    std::size_t N = 0;
    auto put = [&](int C) {
        tokReserve(N + 2);
        if (N + 1 < TokCap) TokBuf[N++] = static_cast<char>(C);
    };
    auto digits = [&](int& C) {
        while (C != EOF && std::isdigit(static_cast<unsigned char>(C)))
            { put(C); C = plangInCh(); }
    };

    tokReserve(1);
    int C = skipBlanks();
    SawAny = (C != EOF);
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
    if (TokBuf) TokBuf[N] = '\0';
}

/// Turbo's read/readln reverse scanNumber's "longest prefix that parses"
/// rule: skip leading blanks, then collect the WHOLE next whitespace-
/// delimited token with no number-shaped filtering at all, so
/// plang_read_i64_turbo/plang_read_f64_turbo can reject anything that is not
/// ENTIRELY a number -- `read(i)` on "12abc" reports Borland/FPC's own
/// "Runtime error 106: Invalid numeric format" (confirmed against
/// `fpc -Mtp`) rather than silently taking 12 -- and can see a leading
/// $/0x/&/% radix prefix before it, which no number-shaped scan would ever
/// let through in the first place.  Shares TokBuf/tokReserve with scanNumber
/// above: the two are never active at once (one read call uses exactly one
/// dialect's scanner), so there is nothing to keep separate.
void scanTokenTurbo(bool &SawAny) {
    std::size_t N = 0;
    auto put = [&](int C) {
        tokReserve(N + 2);
        if (N + 1 < TokCap) TokBuf[N++] = static_cast<char>(C);
    };

    tokReserve(1);
    int C = skipBlanks();
    SawAny = (C != EOF);
    while (C != EOF && C != ' ' && C != '\t' && C != '\n' && C != '\r') {
        put(C);
        C = plangInCh();
    }
    plangInUnget(C);
    if (TokBuf) TokBuf[N] = '\0';
}

/// Strips a Turbo radix prefix from the front of \p Tok, if any, and reports
/// the radix to parse what is left at.  A sign is deliberately not part of
/// this -- see plang_read_i64_turbo's own comment (issue #592) for why a
/// leading sign before the prefix character is stripped separately, by that
/// caller, before this ever runs: decimal's own leading -/+ is left to
/// strtoll/strtod, which already handle it.
int turboRadixPrefix(const char *&Tok) {
    if (Tok[0] == '$') { ++Tok; return 16; }
    if (Tok[0] == '0' && (Tok[1] == 'x' || Tok[1] == 'X')) { Tok += 2; return 16; }
    if (Tok[0] == '&') { ++Tok; return 8; }
    if (Tok[0] == '%') { ++Tok; return 2; }
    return 10;
}

/// Issue #592: does \p Tok start with a sign immediately followed by one of
/// the radix-prefix characters turboRadixPrefix recognizes ($/0x/&/%)?  Real
/// Turbo Pascal/`fpc -Mtp` recognizes a sign at the very start of a numeric
/// token independent of which base prefix follows it -- confirmed
/// empirically: "-$FF" reads as -255, not a malformed token -- so this is
/// checked SEPARATELY from turboRadixPrefix itself (which only ever sees an
/// UNSIGNED token, exactly as it did before this fix) rather than folded
/// into it, so plain decimal's own leading sign keeps flowing straight to
/// strtoll/strtod unchanged (that path already handles it correctly, and
/// stripping the sign there too would reject strtoll's own INT64_MIN
/// parse for a plain, unprefixed negative decimal -- this function is
/// therefore never consulted for a token with no radix-prefix character
/// right after its sign).
bool turboSignedRadixPrefix(const char *Tok, bool &Neg) {
    if (Tok[0] != '-' && Tok[0] != '+') return false;
    const char C1 = Tok[1], C2 = Tok[2];
    if (C1 == '$' || C1 == '&' || C1 == '%' ||
        (C1 == '0' && (C2 == 'x' || C2 == 'X'))) {
        Neg = (Tok[0] == '-');
        return true;
    }
    return false;
}

void consumeLine() {
    int C;
    while ((C = plangInCh()) != EOF && C != '\n') {}
}

} // namespace

extern "C" {

// The width-taking real writers are defined further down with the rest of the
// field-width forms; the ones without a width are those with the default.
void plang_write_f64_e(double V, int64_t W, int8_t Upper);
void plang_write_f64_f(double V, int64_t W, int64_t D, int8_t Upper);

/// Defined with the other runtime error reporters in plang_sys.cpp.
[[noreturn]] void plang_err_field_width(int64_t W);
[[noreturn]] void plang_err_read_format(const char *Op);
[[noreturn]] void plang_err_read_int_range(const char *Op, const char *Tok);
/// Turbo's own numbered run-time error reporter (plang_sys.cpp) -- kept for
/// every OTHER numbered Turbo run-time error this file can still raise
/// (range/overflow-shaped conditions with no InOutRes/{$I-} character at
/// all).  It is deliberately NOT used any more for the "12abc"-shaped
/// malformed-numeric-token case below -- see plang_read_i64_turbo's own
/// comment for why that one specific case was moved onto the InOutRes path
/// instead (a real bug, not a design choice: Borland/`fpc -Mtp` treats a
/// malformed numeric read as an ordinary I/O failure, gated by {$I-} like
/// any other).
[[noreturn]] void plang_tp_runerror(int64_t Code);

/// -std=turbo only: the single InOutRes global -- DEFINED once in
/// runtime/plang_sys.cpp; see that file's own definition and
/// runtime/plang_file.cpp's identical `extern` for the full rationale
/// (int64_t, not Borland's 16-bit Word; one definition, every object file
/// that touches it only declares).
extern int64_t plang_tp_inoutres;

/// -std=turbo only: sets InOutRes to \p Code, but ONLY when InOutRes does
/// not already hold a pending, unread error -- the IDENTICAL "first pending
/// error survives" contract runtime/plang_file.cpp's own
/// setInOutResIfClear implements (see that function's own comment for the
/// full rationale and the `fpc -Mtp` field practice it was found against,
/// PR #481). That helper is `static` (file-local) to plang_file.cpp, so
/// this is a separate, byte-for-byte identical twin rather than a shared
/// declaration -- there is no header both .cpp files already include that
/// would be a more natural home for one without disturbing either file's
/// existing "helpers stay static, next to their callers" convention, and a
/// single-line body has no drift risk to guard against by sharing it.
static void setInOutResIfClear(int64_t Code) {
    if (plang_tp_inoutres == 0) plang_tp_inoutres = Code;
}

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

// Upper (below, and throughout this file): Turbo writes TRUE/FALSE where
// ISO/EP §6.9.3.5 writes true/false, and CodeGen resolves which spelling a
// call site wants from LangOptions.turbo() -- a compile-time-constant fact
// about the whole call site, not a runtime dialect check -- and passes it in
// as this plain i8 flag, the same way it already passes a field width or a
// real's format profile.  0 keeps every existing (pre-Turbo) call site's
// output byte-for-byte unchanged.
void plang_write_i64 (int64_t     V) { plangOutFmt("%" PRId64, V); }
// Turbo's QWord (64-bit unsigned) is the one ordinal a signed formatter gets
// wrong: every narrower unsigned rung (Byte/Word/Cardinal/LongWord) is
// zero-extended to i64 before it ever reaches a write call (CodeGen's
// widening in BuiltinIO.cpp), so its value never sets the i64 sign bit and
// %PRId64/%PRIu64 agree byte for byte -- only a genuinely 64-bit-wide value
// can disagree, and CodeGen routes exactly that one case here instead of
// plang_write_i64.
void plang_write_u64 (uint64_t    V) { plangOutFmt("%" PRIu64, V); }
void plang_write_f64 (double      V, int8_t Upper) { plang_write_f64_e(V, PlangRealWidth, Upper); }
// See PlangSingleMaxDecPlaces's own comment (plang_real.h): a Single is
// promoted to double before it ever reaches this file, so the only thing
// that distinguishes its default write from a genuine double's is the
// significant-digit cap CodeGen selects by routing here instead of
// plang_write_f64.
void plang_write_f32 (double      V, int8_t Upper) {
    char Buf[PlangRealMaxChars];
    plangOutN(Buf, plangFormatReal(Buf, V, PlangRealWidth,
                   Upper ? PlangRealProfileTurbo : PlangRealProfileISO,
                   PlangSingleMaxDecPlaces));
}
void plang_write_bool(int8_t      V, int8_t Upper) {
    plangOutStr(Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"));
}
void plang_write_char(int8_t      V) { plangOutCh(static_cast<unsigned char>(V)); }
void plang_write_str (const char *S) { plangOutStr(S ? S : ""); }

// ---- writeln (with trailing newline) ----

void plang_writeln_i64 (int64_t     V) { plangOutFmt("%" PRId64, V); plangOutCh('\n'); }
void plang_writeln_u64 (uint64_t    V) { plangOutFmt("%" PRIu64, V); plangOutCh('\n'); }
void plang_writeln_f64 (double      V, int8_t Upper) { plang_write_f64(V, Upper); plangOutCh('\n'); }
void plang_writeln_f32 (double      V, int8_t Upper) { plang_write_f32(V, Upper); plangOutCh('\n'); }
void plang_writeln_bool(int8_t      V, int8_t Upper) { plang_write_bool(V, Upper); plangOutCh('\n'); }
void plang_writeln_char(int8_t      V) { plangOutCh(static_cast<unsigned char>(V)); plangOutCh('\n'); }
void plang_writeln_str (const char *S) { plangOutStr(S ? S : ""); plangOutCh('\n'); }
void plang_writeln     ()              { plangOutCh('\n'); }

// ---- EP §6.9.3.6: a complex value is written as a parenthesized real pair ----

void plang_write_cplx  (double Re, double Im, int8_t Upper) {
    plangOutCh('(');
    plang_write_f64(Re, Upper);
    plangOutCh(',');
    plang_write_f64(Im, Upper);
    plangOutCh(')');
}
void plang_writeln_cplx(double Re, double Im, int8_t Upper)
    { plang_write_cplx(Re, Im, Upper); plangOutCh('\n'); }
void plang_write_cplx_w(double Re, double Im, int64_t W, int64_t D, int8_t Upper) {
    // The width applies to each component, as it does for the two reals the
    // pair is written from — and so does the representation, which is why this
    // goes through the real writers rather than formatting the pair itself.
    // plang_write_f64_f already picks between "%*.*f" and the exponential
    // fallback on D's sign, which used to be duplicated here inline.
    plangOutCh('(');
    plang_write_f64_f(Re, W, D, Upper);
    plangOutCh(',');
    plang_write_f64_f(Im, W, D, Upper);
    plangOutCh(')');
}
void plang_writeln_cplx_w(double Re, double Im, int64_t W, int64_t D, int8_t Upper)
    { plang_write_cplx_w(Re, Im, W, D, Upper); plangOutCh('\n'); }

// ---- read ----

void plang_read_i64 (int64_t *P) {
    bool SawAny = false;
    scanNumber(/*Real=*/false, SawAny);
    if (!SawAny) { *P = 0; return; }             // issue #284: past EOF is a defined, consistent zero
    char* End = TokBuf;
    errno = 0;
    const long long V = TokBuf ? std::strtoll(TokBuf, &End, 10) : 0;
    if (!TokBuf || End == TokBuf) plang_err_read_format("read");        // issue #236
    if (errno == ERANGE) plang_err_read_int_range("read", TokBuf);      // issue #240
    *P = static_cast<int64_t>(V);
}
void plang_read_f64 (double  *P) {
    bool SawAny = false;
    scanNumber(/*Real=*/true, SawAny);
    if (!SawAny) { *P = 0.0; return; }            // issue #284
    char* End = TokBuf;
    const double V = TokBuf ? std::strtod(TokBuf, &End) : 0.0;
    if (!TokBuf || End == TokBuf) plang_err_read_format("read");        // issue #236
    // A real that overflows to +/-HUGE_VAL is left alone, matching the
    // runtime's existing policy for real arithmetic generally (an IEEE
    // infinity or NaN, not a trap) -- issue #240 is scoped to integers,
    // whose type has no infinity to fall back on.
    *P = V;
}
void plang_read_char(int8_t  *P) {
    const int C = plangInCh();
    *P = (C == EOF) ? 0 : static_cast<int8_t>(C);
}

// QWord (64-bit unsigned) is the one ordinal plang_read_i64's strtoll cannot
// read in full: a value past INT64_MAX (say the QWord max,
// 18446744073709551615) is a legitimate QWord value but ERANGEs a signed
// parse.  Only Width 64 needs this -- Word/Cardinal/LongWord's ranges all fit
// inside int64_t, so plang_read_i64 already reads them correctly and
// CodeGen's emitReadArg only ever routes a QWord destination here.
//
// A leading '-' is rejected as a malformed token rather than handed to
// strtoull, which would silently accept it and wrap (C's own unsigned-parse
// rule): checked against `fpc -Mtp`, reading a negative token into a QWord
// variable is runtime error 106, not a large wrapped value.
void plang_read_u64 (uint64_t *P) {
    bool SawAny = false;
    scanNumber(/*Real=*/false, SawAny);
    if (!SawAny) { *P = 0; return; }              // issue #284: past EOF is a defined, consistent zero
    if (TokBuf && TokBuf[0] == '-') plang_err_read_format("read");
    char* End = TokBuf;
    errno = 0;
    const unsigned long long V = TokBuf ? std::strtoull(TokBuf, &End, 10) : 0;
    if (!TokBuf || End == TokBuf) plang_err_read_format("read");        // issue #236
    if (errno == ERANGE) plang_err_read_int_range("read", TokBuf);      // issue #240
    *P = static_cast<uint64_t>(V);
}

// ---- Turbo read: whole-token, entire-token-must-parse, with $/0x/&/% radix
// prefixes (confirmed against `fpc -Mtp`; see scanTokenTurbo's own comment) --

// Tier 3 gap fix: a malformed token (e.g. "12abc") is an ORDINARY I/O
// failure in real Turbo Pascal/`fpc -Mtp`, subject to {$I-}/{$I+} exactly
// like every other InOutRes code -- confirmed empirically: under {$I-},
// `fpc -Mtp` sets IOResult to 106, assigns the destination variable 0, and
// lets the program keep running; under the default {$I+} it aborts with
// "Runtime error 106". This function used to call plang_tp_runerror(106)
// directly on a malformed token -- a `[[noreturn]]` reporter that aborts
// unconditionally, INSIDE the read call, before control ever returns to
// the caller -- so the existing emitIoCheckIfNeeded machinery
// (lib/CodeGen/CGProcCall.cpp), already wired in after every read/readln
// statement and already correctly honoring {$I-}/{$I+} for every OTHER
// Turbo I/O failure, never got a chance to run: the process had already
// exited. Fixed the same way runtime/plang_file.cpp's own read/write
// entry points already handle a non-abort failure: set the destination to
// 0, record InOutRes via setInOutResIfClear (the same "a pending, unread
// error is not overwritten" contract, PR #481), and return normally,
// leaving emitIoCheckIfNeeded to decide whether to abort.
//
// A magnitude beyond int64_t (ERANGE) gets the SAME numbered error as a
// malformed token: `fpc -Mtp` reports 106 for "99999999999999999999" too,
// not a distinct overflow code, since its own integer parser has nowhere
// else to put a value that big either. A magnitude that fits int64_t but
// not Turbo's own 16-bit Integer (e.g. "40000") is NOT an error here --
// checked against `fpc -Mtp`, that wraps to -25536 rather than trapping --
// so no range check happens in this function at all; CoerceToType's
// ordinary truncation on the way into a narrower destination
// (BuiltinIO.cpp's emitReadArg) reproduces the wraparound on its own.
//
// Issue #592: a sign immediately followed by a radix-prefix character
// (e.g. "-$FF") is stripped by turboSignedRadixPrefix BEFORE Tok ever
// reaches turboRadixPrefix, and reapplied to the parsed magnitude below --
// see that helper's own comment for why this is a separate check rather
// than folded into turboRadixPrefix itself.  A plain decimal token's own
// sign (with no prefix following it) is left untouched here and reaches
// strtoll exactly as it always did.
void plang_read_i64_turbo(int64_t *P) {
    bool SawAny = false;
    scanTokenTurbo(SawAny);
    if (!SawAny) { *P = 0; return; }               // issue #284: past EOF is a defined, consistent zero
    const char *Tok = TokBuf ? TokBuf : "";
    bool Neg = false;
    if (turboSignedRadixPrefix(Tok, Neg)) ++Tok;
    const int Radix = turboRadixPrefix(Tok);
    char *End = const_cast<char *>(Tok);
    errno = 0;
    long long V = *Tok ? std::strtoll(Tok, &End, Radix) : 0;
    // The ENTIRE token must parse -- not just a prefix of it (scanNumber's
    // own ISO/EP rule, reversed here) -- so *End must land on the token's own
    // terminating NUL, not partway through it.
    if (!*Tok || *End != '\0' || errno == ERANGE) {
        *P = 0;
        setInOutResIfClear(106);
        return;
    }
    if (Neg) V = -V;
    *P = static_cast<int64_t>(V);
}

// The Turbo twin of plang_read_u64 above, for the identical QWord-only
// reason.  A leading '-' is rejected up front, before turboRadixPrefix ever
// gets a chance to strip a following '$' -- checked against `fpc -Mtp`,
// "-$FF" read into a QWord is runtime error 106, exactly as a plain "-5" is
// -- and, per plang_read_i64_turbo's own comment just above, a real I/O
// failure gated by {$I-}/{$I+}, not an unconditional abort.
void plang_read_u64_turbo(uint64_t *P) {
    bool SawAny = false;
    scanTokenTurbo(SawAny);
    if (!SawAny) { *P = 0; return; }               // issue #284: past EOF is a defined, consistent zero
    const char *Tok = TokBuf ? TokBuf : "";
    if (*Tok == '-') {
        *P = 0;
        setInOutResIfClear(106);
        return;
    }
    const int Radix = turboRadixPrefix(Tok);
    char *End = const_cast<char *>(Tok);
    errno = 0;
    const unsigned long long V = *Tok ? std::strtoull(Tok, &End, Radix) : 0;
    if (!*Tok || *End != '\0' || errno == ERANGE) {
        *P = 0;
        setInOutResIfClear(106);
        return;
    }
    *P = static_cast<uint64_t>(V);
}

void plang_read_f64_turbo(double *P) {
    bool SawAny = false;
    scanTokenTurbo(SawAny);
    if (!SawAny) { *P = 0.0; return; }              // issue #284
    const char *Tok = TokBuf ? TokBuf : "";
    // No radix prefix for a real read: `fpc -Mtp` reports 106 for a real
    // read given "$FF" too, which strtod already refuses on its own (nothing
    // starting with '$' is a valid C float), so there is nothing to strip
    // here the way plang_read_i64_turbo strips one.
    char *End = const_cast<char *>(Tok);
    const double V = *Tok ? std::strtod(Tok, &End) : 0.0;
    // See plang_read_i64_turbo's own comment: a malformed token is an
    // ordinary {$I-}/{$I+}-gated I/O failure, not an unconditional abort.
    if (!*Tok || *End != '\0') {
        *P = 0.0;
        setInOutResIfClear(106);
        return;
    }
    // A real that overflows to +/-HUGE_VAL is left alone, matching the ISO/EP
    // reader's own policy just above (and the runtime's policy for real
    // arithmetic generally): an IEEE infinity or NaN, not a trap.
    *P = V;
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
// See plang_write_u64's own comment: the field-width form needs the identical
// unsigned/signed split, for the identical reason.
void plang_write_u64_w (uint64_t V, int64_t W) {
    plangOutFmt("%*" PRIu64, checkedWidth(W), V);
}
void plang_write_f64_e (double  V, int64_t W, int8_t Upper) {
    char Buf[PlangRealMaxChars];
    plangOutN(Buf, plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO));
}
// See plang_write_f32's own comment: the field-width exponential form needs
// the identical significant-digit cap, for the identical reason -- a wide
// field asked for here must not grow into digits a binary32 never had.
void plang_write_f32_e (double  V, int64_t W, int8_t Upper) {
    char Buf[PlangRealMaxChars];
    plangOutN(Buf, plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO,
                   PlangSingleMaxDecPlaces));
}
// A negative FracDigits falls back to the same exponential format omitting
// the decimals clause entirely produces, exactly as plang_write_cplx_w's own
// per-component formatting already did before it started calling this.
//
// -std=turbo only: issue #677.  V used to go straight into printf's
// `%*.*f`, which prints a double's EXACT binary value out to D decimals --
// correct arithmetic, but not what a Pascal reader expects once D asks for
// more digits than a double actually carries (15-17 significant decimals):
// the tail past that point is the binary REPRESENTATION showing through,
// not the VALUE the program wrote.  See plang_real.h's PlangRealFixedShape
// for the full rationale and the `fpc -Mtp` field practice this matches.
//
// \p Upper is the same CodeGen-resolved isTurbo() flag every OTHER
// dialect-sensitive parameter in this file already is (see plang_write_bool
// for the convention) -- there is no `_turbo` sibling for THIS function the
// way plang_file.cpp's own writers get one (Write/Writeln to stdout have no
// PascalFile* to dispatch a dialect-specific runtime symbol from at the
// CodeGen call site the way a file destination does), so the dialect check
// has to happen here, at runtime, gated on that flag instead.  Capping is
// Turbo-only and deliberately does NOT apply to ISO/EP: EP's own
// iso7185pat.pas acceptance test (test/Acceptance/) exercises exactly this
// "precision dropoff" exact-binary-expansion behavior on purpose
// (`writeln(i+0.234...:1:i)` for i up to 20), so capping ISO/EP's own
// output the same way would be a real conformance regression, not a fix --
// confirmed by actually breaking that acceptance test during this item's
// development.  Inf/NaN keep the original printf call untouched (rounding
// "to 17 significant digits" is not a meaningful question for either).
void plang_write_f64_f (double  V, int64_t W, int64_t D, int8_t Upper) {
    if (D < 0) { plang_write_f64_e(V, W, Upper); return; }
    const int Dc = checkedWidth(D);
    PlangRealFixedShape Shape;
    if (!Upper || !std::isfinite(V) || !plangRealFixedNeedsCap(V, Dc, &Shape)) {
        plangOutFmt("%*.*f", checkedWidth(W), Dc, V);
        return;
    }
    const int64_t IntDigits = plangRealFixedIntDigits(Shape);
    const int64_t Len = (Shape.Negative ? 1 : 0) + IntDigits + (Dc > 0 ? 1 + Dc : 0);
    for (int64_t I = Len; I < checkedWidth(W); ++I) plangOutCh(' ');
    if (Shape.Negative) plangOutCh('-');
    for (int64_t Wt = IntDigits - 1; Wt >= 0; --Wt)
        plangOutCh(plangRealFixedDigitAt(Shape, Wt));
    if (Dc > 0) {
        plangOutCh('.');
        for (int64_t Wt = -1; Wt >= -Dc; --Wt)
            plangOutCh(plangRealFixedDigitAt(Shape, Wt));
    }
}
// §6.9.3.6: the field is exactly TotalWidth characters wide, so a string
// longer than the field loses its tail rather than widening it — the `%*s` a
// field width otherwise maps onto pads but never truncates.  A negative width
// truncates nothing and pads nothing: the value is written in full, as if no
// width had been given at all.  Every rule above is ISO/EP's; Turbo reverses
// the "loses its tail" part -- \p NoTrunc, another CodeGen-resolved i8 flag
// (see plang_write_bool's own comment on the convention), makes W a MINIMUM
// instead: the value is always written in full, and W only ever adds padding,
// never removes text.  Checked directly against `fpc -Mtp`: `'hello':2`
// writes "hello" whole, and even `'hello':0` -- the one case ISO/EP write
// NOTHING for -- writes "hello" too, since a minimum of zero is trivially
// met.  A negative W is unaffected by NoTrunc: it already means "write the
// value in full, unpadded" for both dialects (see noPadIfNegative above),
// which is exactly what NoTrunc would ask for anyway.
static void plangOutPadded(const char* S, int64_t W, int8_t NoTrunc) {
    const size_t Len = S ? std::strlen(S) : 0;
    if (!NoTrunc && W == 0) return;
    if (W < 0) { if (Len) plangOutN(S, Len); return; }
    // W > 0 here (or W == 0 under NoTrunc, where checkedWidth(0) == 0 and the
    // pad loop below simply does not run): the numeric/char writers above
    // hand W to printf's `%*d`/`%*c`, so checkedWidth's INT32_MAX trap guards
    // their width argument from silently truncating (issue #15). This writer
    // paces its own padding loop by hand instead of going through printf, so
    // without the same call an oversized W (write(s:maxint)) just pads one
    // character at a time until it gets there -- no truncation to guard
    // against, but an unbounded amount of CPU and output for a value nothing
    // could ever read (issue #247).
    const auto Width = static_cast<size_t>(checkedWidth(W));
    for (size_t I = Len; I < Width; ++I) plangOutCh(' ');
    if (NoTrunc) { if (Len) plangOutN(S, Len); return; }
    if (Len) plangOutN(S, Len < Width ? Len : Width);
}

// §6.9.3.5 writes a boolean as the char-string 'true'/'false' ('TRUE'/'FALSE'
// under Turbo -- Upper, see plang_write_bool's own comment) would be written,
// which is why it truncates (or, under Turbo's NoTrunc, does not) too.
void plang_write_bool_w(int8_t V, int64_t W, int8_t Upper, int8_t NoTrunc) {
    plangOutPadded(Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"), W, NoTrunc);
}
// AlwaysWrite (another CodeGen-resolved i8 flag): ISO §6.10.3.1(u) makes a
// zero char width write nothing; Turbo's own zero-width char write still
// writes the character (checked directly against `fpc -Mtp`: `write('x':0)`
// writes "x"), consistent with field widths being minimums there generally
// (plangOutPadded's own NoTrunc, just applied to a type padding never
// truncates for in the first place -- a char is always exactly one
// character, so the only thing a width can ever do to it is this all-or-
// nothing zero case).
void plang_write_char_w(int8_t V, int64_t W, int8_t AlwaysWrite) {
    if (W == 0 && !AlwaysWrite) return;
    if (W < 0) { plangOutCh(static_cast<unsigned char>(V)); return; }
    plangOutFmt("%*c", checkedWidth(W), static_cast<unsigned char>(V));
}
void plang_write_str_w (const char *S, int64_t W, int8_t NoTrunc) { plangOutPadded(S, W, NoTrunc); }

// ---- writeln with field-width ----

void plang_writeln_i64_w (int64_t V, int64_t W) { plang_write_i64_w(V, W); plangOutCh('\n'); }
void plang_writeln_u64_w (uint64_t V, int64_t W) { plang_write_u64_w(V, W); plangOutCh('\n'); }
void plang_writeln_f64_e (double  V, int64_t W, int8_t Upper) { plang_write_f64_e(V, W, Upper); plangOutCh('\n'); }
void plang_writeln_f32_e (double  V, int64_t W, int8_t Upper) { plang_write_f32_e(V, W, Upper); plangOutCh('\n'); }
void plang_writeln_f64_f (double  V, int64_t W, int64_t D, int8_t Upper)
    { plang_write_f64_f(V, W, D, Upper); plangOutCh('\n'); }
void plang_writeln_bool_w(int8_t V, int64_t W, int8_t Upper, int8_t NoTrunc)
    { plang_write_bool_w(V, W, Upper, NoTrunc); plangOutCh('\n'); }
void plang_writeln_char_w(int8_t V, int64_t W, int8_t AlwaysWrite)
    { plang_write_char_w(V, W, AlwaysWrite); plangOutCh('\n'); }
void plang_writeln_str_w (const char *S, int64_t W, int8_t NoTrunc)
    { plang_write_str_w(S, W, NoTrunc); plangOutCh('\n'); }

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

/// The Turbo string[N] (ShortString) sibling of plang_writestr_end just
/// above: same capture-buffer bracketing, but a ONE-byte length prefix (no
/// [8-byte header, then bytes] layout, no space-padding -- ShortString has
/// no fixed-string-type sibling the way EP's writestr_end_fixed serves) at
/// \p S, clamped at \p Cap the same TRUNCATING way every other
/// plang_sstr_*-family assignment does (plang_sstr.cpp), not EP's
/// error-on-overflow.  Cap is clamped to 255 defensively -- the length
/// byte's own ceiling -- the same effCap every plang_sstr_* function applies,
/// even though Str's own destination is always Sema-sized at exactly 255 or
/// less already (Builtins.def's own comment on Copy/Concat/StringOfChar).
void plang_writestr_end_sstr(void *S, int64_t Cap) {
    std::size_t Len = 0;
    const char* Buf = nullptr;
    if (CapDepth > 0) {
        CapFrame& F = CapStack[--CapDepth];
        Len = F.Len;
        Buf = F.Buf;
    }
    if (!S) return;
    int64_t ecap = Cap;
    if (ecap > 255) ecap = 255;
    if (ecap < 0)   ecap = 0;
    auto L = static_cast<int64_t>(Len);
    if (L > ecap) L = ecap;
    auto* Base = static_cast<unsigned char*>(S);
    Base[0] = static_cast<unsigned char>(L);
    if (L > 0) std::memcpy(Base + 1, Buf, static_cast<std::size_t>(L));
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
