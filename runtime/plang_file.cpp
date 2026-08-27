/// plang_file.cpp — Pascal file-variable I/O (C++23, ISO 7185)
///
/// Runtime representation
/// ----------------------
/// A Pascal file variable is a PascalFile struct.  Generated code passes a
/// pointer to every file operation.
///
/// The \c buf field implements the one-character lookahead required by eof/eoln:
///   PLANG_FILE_UNINIT (-2) : window not yet primed (before first get)
///   EOF               (-1) : end-of-file
///   ≥ 0                    : current window character
///
/// The window character is *pushed back* onto the stream rather than held only
/// in the field, so the file's position and the C stream's position agree.
/// That matters because 'input' names the same stdin that a bare read reaches
/// through getchar: if priming consumed the character, the two views would
/// disagree by one and the program would lose its first byte.
///
/// The buffer variable
/// -------------------
/// ISO §6.5.5 gives every file variable f a buffer variable f^ holding the
/// component at the current position, which reset and get fill and put writes
/// out.  The \c Comp field is that component, read by peeking the same way the
/// character window does — the component is read and the position restored —
/// so the position the program can observe through position and SeekRead is
/// always the position of f^ itself, and not one component past it.

#include "plang_real.h"

#include "plang/Basic/PascalFileLayout.h"
#include "plang/Basic/RequiredRecordLayouts.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

namespace plang {

// PascalFile, PlangBinding and PlangFileUninit are declared in
// plang/Basic/PascalFileLayout.h, which codegen reads as well: fileStructType()
// checks the LLVM type it builds against that struct field by field, so the two
// encodings of one layout can no longer drift apart.  A `sizeof` assert used to
// stand here, and could not see the codegen side at all.

extern "C" {

// ---- Internal helpers ----

/// Defined with the binding table further down; reset and rewrite need it to
/// honor a name established by an earlier bind (EP §6.7.5.6).
static const char* findBinding(PascalFile *F);

/// Defined with the binding table further down; reset and rewrite call this
/// too now (issue #239): a name given directly to either -- not just one
/// given through bind -- is retained the same way, so a later call with no
/// name reopens that same external entity instead of diverting to fresh,
/// unnamed internal storage.
static void setBinding(PascalFile *F, const char *Name);

/// Defined with the other runtime error reporters in plang_sys.cpp.
[[noreturn]] void plang_err_bind_already_bound(void);
[[noreturn]] void plang_err_binding_table_full(void);
[[noreturn]] void plang_err_cannot_open(const char *Msg);
[[noreturn]] void plang_err_field_width(int64_t W);
[[noreturn]] void plang_err_file_wrong_mode(const char *Op);
[[noreturn]] void plang_err_seek_failed(const char *Op, int64_t N);

/// Look at the next character without consuming it.
static void prime(PascalFile *F) {
    F->Buf = std::fgetc(F->Fp);
    if (F->Buf != EOF) std::ungetc(F->Buf, F->Fp);
}

/// Fill the lookahead window if nothing has needed it yet.
///
/// Priming reads a character, and where the stream is a terminal that waits
/// for one to be typed.  §6.10 has `input` reset as the program starts, but a
/// program that never reads it must not stop for a keystroke on its way to the
/// first line it prints — which is what `program count(input, output)` did,
/// hanging with nothing on the screen before its first writeln.
///
/// Putting the read off costs nothing, because priming pushes the character
/// back with ungetc and so leaves the position where it was: a stream that has
/// never been primed and one that has are at the same place, and only the
/// operations that ask what the window *holds* can tell the difference.  Those
/// are the ones that call this.
static void ensurePrimed(PascalFile *F) {
    if (F->Buf == PlangFileUninit && F->Readable && F->Fp) prime(F);
}

/// Consume the window character and look at the one after it.
static int advance(PascalFile *F) {
    const int C = std::fgetc(F->Fp);
    prime(F);
    return C;
}

/// The position has moved, so whatever f^ held is no longer the component
/// there.  The next access to f^ reads the new one.
static void unloadComponent(PascalFile *F) { F->CompLoaded = 0; }

static void abortIfClosed(PascalFile *F, const char *Op) {
    if (!F || !F->Fp) {
        std::fprintf(stderr, "plang runtime: file not open in '%s'\n", Op);
        std::abort();
    }
}

/// ISO §6.7.5.6: a write/read against a file positioned or opened for the
/// other direction fails at the C stream level -- fread/fwrite return short,
/// fprintf/fputc/fputs return a negative/EOF sentinel, and in every one of
/// those cases the stream's own error indicator is set.  A stdio call that
/// merely hit real end-of-file, or an fscanf that merely failed to match its
/// format, sets neither -- so checking ferror here (rather than treating any
/// short return as this violation) traps exactly the wrong-mode case and
/// leaves the eof and malformed-input cases exactly as they behaved before.
/// F->Readable is not a substitute for this: seekread/seekupdate set it
/// without reopening F->Fp, so it does not reflect the mode the stream was
/// actually opened in (the gap issue #124 was filed against).
static void trapOnStreamError(PascalFile *F, const char *Op) {
    if (std::ferror(F->Fp)) plang_err_file_wrong_mode(Op);
}

/// fscanf does not go through trapOnStreamError: on glibc, scanning a stream
/// opened in the wrong direction returns EOF (as a genuine end-of-file read
/// also does) but -- unlike fread/fwrite/fgetc/fputc above -- leaves the
/// stream's own ferror() indicator clear, so ferror cannot tell the two
/// apart here. feof() can: a real end-of-file sets it, a wrong-mode failure
/// does not, so only the latter is this violation.
static void trapOnScanError(PascalFile *F, int MatchCount, const char *Op) {
    if (MatchCount == EOF && !std::feof(F->Fp)) plang_err_file_wrong_mode(Op);
}

/// Issue #152: extends the #124 mode trap to an internal (unbound) file.
/// Such a file is backed by tmpfile(), which glibc always opens "w+b" --
/// genuinely bidirectional at the C level -- regardless of which way the
/// Pascal file is currently facing, so a wrong-direction stdio call there
/// succeeds outright: ferror/feof never fires, and trapOnStreamError /
/// trapOnScanError have nothing to see.  F->Readable is the only place the
/// intended direction is recorded for such a file (set by reset/rewrite and
/// their seek* counterparts, same as for a named file), so this checks it
/// directly and traps through the same plang_err_file_wrong_mode a named
/// file's ferror/feof check reaches.
///
/// Only PlangBindTemp is checked: a named or standard stream is already
/// protected by its own C-level open mode (fopen "r"/"w"), so adding this
/// check there would be redundant at best.  Readable's third value, 2, is
/// set by extend/update/seekupdate for a file genuinely opened both ways
/// (named files use "r+b" there too) and is deliberately exempt: those modes
/// allow interleaved reads and writes by design, not by omission.
static void trapOnWrongDirection(PascalFile *F, const char *Op, int8_t WantWrite) {
    if (F->Binding != PlangBindTemp || F->Readable == 2) return;
    if ((WantWrite && F->Readable == 1) || (!WantWrite && F->Readable == 0))
        plang_err_file_wrong_mode(Op);
}

// ---- open / close ----

static void closeStream(PascalFile *F) {
    if (F->Fp && F->Fp != stdin && F->Fp != stdout) std::fclose(F->Fp);
    F->Fp      = nullptr;
    F->Binding = PlangBindNone;
}

static std::FILE *openTemp(const char *Op) {
    std::FILE *Fp = std::tmpfile();
    if (!Fp) {
        char Msg[128];
        std::snprintf(Msg, sizeof(Msg), "cannot create temporary file in '%s'", Op);
        plang_err_cannot_open(Msg);
    }
    return Fp;
}

/// §6.4.3.5: a text file holds lines, and a line is only a line once a line
/// marker ends it.  A file put together with write and no closing writeln has
/// a part of one left over, and reading it back has to see the line the writer
/// meant rather than stop short of its end.
static void closeFinalLine(PascalFile *F) {
    if (!F->Fp || F->Readable) return;
    std::fflush(F->Fp);
    const long End = std::ftell(F->Fp);
    if (End <= 0) return;                 // nothing written: no line to close
    if (std::fseek(F->Fp, -1, SEEK_CUR) != 0) return;
    const int Last = std::fgetc(F->Fp);
    if (Last != '\n' && Last != EOF) std::fputc('\n', F->Fp);
    std::clearerr(F->Fp);
}

void plang_reset(PascalFile *F, const char *Name, int8_t IsText) {
    if (IsText) closeFinalLine(F);
    // Issue #239: it is Name as the caller actually passed it -- not
    // whatever the bind() fallback below may replace it with -- that gets
    // retained further down, so which case this is has to be captured now.
    const bool HasExplicitName = Name && Name[0] != '\0';
    // EP §6.7.5.6: a file bound to an external entity opens that entity even
    // when reset is called without an explicit name.
    if (!HasExplicitName) Name = findBinding(F);
    if (!Name || Name[0] == '\0') {
        if (F->Binding == PlangBindStd) {
            // stdin is not rewindable; just re-prime the lookahead window.
        } else if (F->Fp) {
            // Internal file: reposition the existing temporary storage to the
            // start rather than reading stdin.  tmpfile() streams are "w+b",
            // so data just written by rewrite/writeln is readable after this.
            std::fflush(F->Fp);
            std::rewind(F->Fp);
        } else {
            F->Fp      = openTemp("reset");
            F->Binding = PlangBindTemp;
        }
    } else {
        closeStream(F);
        F->Fp = std::fopen(Name, "r");
        if (!F->Fp) {
            char Msg[512];
            std::snprintf(Msg, sizeof(Msg), "cannot open '%s' for reading", Name);
            plang_err_cannot_open(Msg);
        }
        // Issue #287: opening a directory read-only succeeds at the C level
        // on POSIX -- the open() syscall underneath allows O_RDONLY on one,
        // even though no byte of it can ever be read back -- so left
        // unchecked, eof(f) would come up true immediately and every read
        // would silently see what looks like an ordinary, unremarkable
        // end-of-file instead of the real problem. Checked once, here,
        // rather than in each of this file's several different read paths.
        // rewrite/extend/update need no equivalent check: opening a
        // directory for writing (or read+write) already fails at fopen
        // itself, reaching the plang_err_cannot_open call just above instead.
        struct stat St;
        if (fstat(fileno(F->Fp), &St) == 0 && S_ISDIR(St.st_mode)) {
            std::fclose(F->Fp);
            F->Fp = nullptr;
            char Msg[512];
            std::snprintf(Msg, sizeof(Msg), "'%s' is a directory, not a file", Name);
            plang_err_cannot_open(Msg);
        }
        // Issue #239: retain an explicit name the same way bind() does, so a
        // later reset/rewrite with no name reopens this same external entity
        // instead of silently diverting to fresh, unnamed internal storage.
        if (HasExplicitName) setBinding(F, Name);
    }
    F->Readable = 1;
    unloadComponent(F);
    prime(F);
}

void plang_rewrite(PascalFile *F, const char *Name, int8_t /*IsText*/) {
    // Issue #239: see the identical note in plang_reset -- Name is about to
    // be overwritten by the bind() fallback on the next line, so whether it
    // was the caller's own explicit argument has to be captured first.
    const bool HasExplicitName = Name && Name[0] != '\0';
    if (!HasExplicitName) Name = findBinding(F);
    if ((!Name || Name[0] == '\0') && F->Binding == PlangBindStd) {
        F->Buf      = PlangFileUninit; // rewrite(output) keeps writing to stdout
        F->Readable = 0;
        unloadComponent(F);
        return;
    }
    closeStream(F);
    if (!Name || Name[0] == '\0') {
        F->Fp      = openTemp("rewrite");
        F->Binding = PlangBindTemp;
    } else {
        F->Fp = std::fopen(Name, "w");
        if (!F->Fp) {
            char Msg[512];
            std::snprintf(Msg, sizeof(Msg), "cannot open '%s' for writing", Name);
            plang_err_cannot_open(Msg);
        }
        // Issue #239: retain an explicit name the same way bind() does, so a
        // later reset/rewrite with no name reopens this same external entity
        // instead of silently diverting to fresh, unnamed internal storage.
        if (HasExplicitName) setBinding(F, Name);
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    unloadComponent(F);
}

/// Binds a program file-parameter to a standard stream (ISO §6.10).  Called
/// once at program start for the 'input' and 'output' parameters.
void plang_bind_std(PascalFile *F, int8_t IsInput) {
    closeStream(F);
    F->Binding  = PlangBindStd;
    F->Readable = IsInput ? 1 : 0;
    unloadComponent(F);
    if (IsInput) {
        F->Fp  = stdin;
        // Left unprimed: see ensurePrimed.  Reading a character here would
        // block a program that only writes.
        F->Buf = PlangFileUninit;
    } else {
        F->Fp  = stdout;
        F->Buf = PlangFileUninit;
    }
}

void plang_close(PascalFile *F) {
    closeStream(F);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    std::free(F->Comp);
    F->Comp     = nullptr;
    F->CompSize = 0;
    unloadComponent(F);
}

// ---- status ----

int8_t plang_eof_file(PascalFile *F) {
    abortIfClosed(F, "eof");
    // §6.6.5.2: rewrite(f) leaves eof(f) true, and it stays true for as long as
    // f is being generated — writing happens at the end of the file, so that is
    // where the position always is.  Only a file being inspected can be
    // anywhere else, and only there does the lookahead window answer this.
    if (!F->Readable) return 1;
    ensurePrimed(F);
    return (F->Buf == EOF) ? 1 : 0;
}

int8_t plang_eoln_file(PascalFile *F) {
    abortIfClosed(F, "eoln");
    ensurePrimed(F);
    return (F->Buf == EOF || F->Buf == '\n') ? 1 : 0;
}

// ---- advance / flush ----

// ---- ISO §6.5.5: the buffer variable f^ ----

/// The address of f^, holding the component at the current position.
///
/// A component is read by peeking: it is read and the position put back, so
/// that reading f^ does not move the file on.  A one-byte component rides on
/// the character window instead, because a text file may be a terminal, where
/// there is no position to put back but there is an ungetc.
void *plang_file_buffer(PascalFile *F, int64_t ElemSize, int8_t IsText) {
    abortIfClosed(F, "buffer variable");
    if (ElemSize < 1) ElemSize = 1;
    if (F->CompSize != ElemSize) {
        std::free(F->Comp);
        F->Comp = std::malloc(static_cast<std::size_t>(ElemSize));
        if (!F->Comp) {
            std::fprintf(stderr, "plang runtime: out of memory for a file buffer\n");
            std::abort();
        }
        F->CompSize   = ElemSize;
        F->CompLoaded = 0;
    }
    if (F->CompLoaded) return F->Comp;
    std::memset(F->Comp, 0, static_cast<std::size_t>(ElemSize));
    if (F->Readable) {
        ensurePrimed(F);
        if (ElemSize == 1) {
            // §6.4.3.5: on a text file the line marker is not a character of
            // the file, and f^ reads as a space wherever one stands.
            if (IsText && F->Buf == '\n')      *static_cast<char*>(F->Comp) = ' ';
            else if (F->Buf >= 0) *static_cast<char*>(F->Comp) = static_cast<char>(F->Buf);
        } else {
            const long Pos = std::ftell(F->Fp);
            if (std::fread(F->Comp, static_cast<std::size_t>(ElemSize), 1, F->Fp) != 1)
                std::memset(F->Comp, 0, static_cast<std::size_t>(ElemSize));
            std::clearerr(F->Fp);
            if (Pos >= 0) std::fseek(F->Fp, Pos, SEEK_SET);
            prime(F);
        }
    }
    F->CompLoaded = 1;
    return F->Comp;
}

/// ISO §6.5.5: get(f) moves on to the next component, which f^ then holds.
void plang_get_file(PascalFile *F, int64_t ElemSize) {
    abortIfClosed(F, "get");
    if (ElemSize > 1) {
        std::fseek(F->Fp, ElemSize, SEEK_CUR);
        prime(F);
    } else {
        advance(F);
    }
    unloadComponent(F);
}

/// ISO §6.5.5: put(f) appends f^ to the file.  A file whose f^ was never
/// assigned has nothing to append, which the standard makes an error and this
/// treats as writing nothing.
void plang_put_file(PascalFile *F, int64_t ElemSize) {
    abortIfClosed(F, "put");
    if (!F->Comp) return;
    trapOnWrongDirection(F, "put", 1);
    if (ElemSize < 1) ElemSize = 1;
    std::fwrite(F->Comp, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "put");
    unloadComponent(F);
}

void plang_readln_file(PascalFile *F) {
    abortIfClosed(F, "readln");
    ensurePrimed(F);
    while (F->Buf != EOF && F->Buf != '\n') advance(F);
    if (F->Buf == '\n') advance(F);
    unloadComponent(F);
}

void plang_writeln_file(PascalFile *F) {
    abortIfClosed(F, "writeln");
    trapOnWrongDirection(F, "writeln", 1);
    std::fputc('\n', F->Fp);
    trapOnStreamError(F, "writeln");
}

void plang_page_file(PascalFile *F) {
    abortIfClosed(F, "page");
    trapOnWrongDirection(F, "page", 1);
    std::fputc('\f', F->Fp);
    trapOnStreamError(F, "page");
}

// ---- typed read (text file) ----

void plang_read_file_i64(PascalFile *F, int64_t *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    trapOnScanError(F, std::fscanf(F->Fp, "%" SCNd64, P), "read");
    prime(F);
    unloadComponent(F);
}

void plang_read_file_f64(PascalFile *F, double *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    trapOnScanError(F, std::fscanf(F->Fp, "%lf", P), "read");
    prime(F);
    unloadComponent(F);
}

void plang_read_file_char(PascalFile *F, int8_t *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    ensurePrimed(F);
    if (F->Buf == EOF) { *P = 0; return; }
    // §6.9.1 reads a char as `v := f^; get(f)`, and §6.4.3.5 gives f^ the value
    // of a space at a line marker: the marker separates the lines rather than
    // belonging to one, so reading a file character by character yields a space
    // where each line ends and not the newline the line is stored with.
    const int C = advance(F);
    trapOnStreamError(F, "read");
    *P = static_cast<int8_t>(C == '\n' ? ' ' : C);
    unloadComponent(F);
}

/// Fills the string(N) at S with the rest of the current line.  Excess input
/// is discarded, matching the truncating assignment rule, and the terminator
/// is left in the lookahead so a following readln consumes exactly one line.
void plang_str_read_file(PascalFile *F, void *S, int64_t Cap) {
    abortIfClosed(F, "read");
    auto*   Base = static_cast<char*>(S);
    char*   Data = Base + sizeof(int64_t);
    int64_t Len  = 0;
    trapOnWrongDirection(F, "read", 0);
    ensurePrimed(F);
    while (F->Buf != EOF && F->Buf != '\n') {
        const int C = advance(F);
        trapOnStreamError(F, "read");
        if (Len < Cap) Data[Len++] = static_cast<char>(C);
    }
    *reinterpret_cast<int64_t*>(Base) = Len;
}

/// The fixed-string-type sibling of plang_str_read_file (ISO §6.10.1(e)): no
/// length field, so what plang_str_read_file leaves untouched past Len, this
/// pads with the spaces the standard requires.
void plang_str_read_fixed_file(PascalFile *F, void *Buf, int64_t N) {
    abortIfClosed(F, "read");
    auto*   Data = static_cast<char*>(Buf);
    int64_t Len  = 0;
    trapOnWrongDirection(F, "read", 0);
    ensurePrimed(F);
    while (F->Buf != EOF && F->Buf != '\n') {
        const int C = advance(F);
        trapOnStreamError(F, "read");
        if (Len < N) Data[Len] = static_cast<char>(C);
        ++Len;
    }
    for (int64_t I = Len; I < N; ++I) Data[I] = ' ';
}

/// Writes the string(N) at S, which is not null-terminated.
void plang_str_write_file(PascalFile *F, const void *S, int64_t /*Cap*/) {
    abortIfClosed(F, "write");
    const auto* Base = static_cast<const char*>(S);
    const int64_t Len = *reinterpret_cast<const int64_t*>(Base);
    if (Len > 0) {
        trapOnWrongDirection(F, "write", 1);
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
        trapOnStreamError(F, "write");
    }
}

/// The same, in a field of W characters: right-justified, and truncated rather
/// than widened when it does not fit (§6.9.3.6).  A negative W truncates
/// nothing and pads nothing: the value is written in full, as if no width
/// had been given at all -- see noPadIfNegative below for why this must not
/// fold into the W==0 case, which drops the value's text outright.
void plang_str_write_file_w(PascalFile *F, const void *S, int64_t /*Cap*/,
                            int64_t W) {
    abortIfClosed(F, "write");
    if (W == 0) return;
    trapOnWrongDirection(F, "write", 1);
    const auto* Base = static_cast<const char*>(S);
    int64_t Len = *reinterpret_cast<const int64_t*>(Base);
    if (Len < 0) Len = 0;
    if (W < 0) {
        if (Len > 0)
            std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    for (int64_t I = Len; I < W; ++I) std::fputc(' ', F->Fp);
    if (Len > W) Len = W;
    if (Len > 0)
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
    trapOnStreamError(F, "write");
}

// ---- typed write (text file) ----

// Defined below with the rest of the field-width forms; the real writer with no
// width is the same one with the default.
void plang_write_file_f64_e(PascalFile *F, double V, int64_t W);
void plang_write_file_f64_f(PascalFile *F, double V, int64_t W, int64_t D);

void plang_write_file_i64 (PascalFile *F, int64_t     V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fprintf(F->Fp, "%" PRId64, V); trapOnStreamError(F, "write"); }
void plang_write_file_f64 (PascalFile *F, double      V) { plang_write_file_f64_e(F, V, PlangRealWidth); }
void plang_write_file_bool(PascalFile *F, int8_t      V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fputs(V ? "true" : "false", F->Fp); trapOnStreamError(F, "write"); }
void plang_write_file_char(PascalFile *F, int8_t      V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fputc(static_cast<unsigned char>(V), F->Fp); trapOnStreamError(F, "write"); }
void plang_write_file_str (PascalFile *F, const char *S) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fputs(S ? S : "", F->Fp); trapOnStreamError(F, "write"); }

// ---- typed write with a field width (ISO §6.9.3.1) ----
//
// Same formats as the stdout writers in plang_io.cpp.  A width of zero writes
// nothing for the fixed-size forms and no padding for the numeric ones, which
// is what a printf width of zero already does.
//
// ISO §6.10.3.1 calls a negative TotalWidth or FracDigits "an error" (§3.2's
// weaker class, which a processor may leave undetected) rather than saying
// what it means.  Checked directly against FPC, in both its default and
// Turbo-compatibility modes: neither treats a negative width as the
// zero-width rule above -- it behaves as though no width had been written
// at all, uniformly across every type.  Before this, `%*d`/`%*c`/`%*.*f` fed
// a negative width straight to printf, whose `*` takes a negative argument
// as its own left-justify flag with the field set to the value's absolute
// value -- an accident of libc, not a considered choice.
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

void plang_write_file_i64_w (PascalFile *F, int64_t V, int64_t W) {
    abortIfClosed(F,"write");
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*" PRId64, checkedWidth(W), V);
    trapOnStreamError(F, "write");
}
void plang_write_file_f64_e (PascalFile *F, double V, int64_t W) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W);
    std::fwrite(Buf, 1, N, F->Fp);
    trapOnStreamError(F, "write");
}
// A negative FracDigits falls back to the same exponential format omitting
// the decimals clause entirely produces, exactly as plang_write_file_cplx_w's
// own per-component formatting already did before it started calling this.
void plang_write_file_f64_f (PascalFile *F, double V, int64_t W, int64_t D) {
    abortIfClosed(F,"write");
    if (D < 0) { plang_write_file_f64_e(F, V, W); return; }
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*.*f", checkedWidth(W), checkedWidth(D), V);
    trapOnStreamError(F, "write");
}
// §6.9.3.6: the field is exactly W characters, so a longer string loses its
// tail; the `%*s` a width otherwise maps onto pads but never truncates.
// §6.9.3.5 writes a boolean as its char-string would be written, truncation
// included.  A negative W is written in full, as if no width had been given.
static void writePadded(PascalFile *F, const char *S, int64_t W) {
    if (W == 0) return;
    trapOnWrongDirection(F, "write", 1);
    const std::size_t Len = S ? std::strlen(S) : 0;
    if (W < 0) {
        if (Len) std::fwrite(S, 1, Len, F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    const auto Width = static_cast<std::size_t>(W);
    for (std::size_t I = Len; I < Width; ++I) std::fputc(' ', F->Fp);
    if (Len) std::fwrite(S, 1, Len < Width ? Len : Width, F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_bool_w(PascalFile *F, int8_t V, int64_t W)
    { abortIfClosed(F,"write"); writePadded(F, V ? "true" : "false", W); }
void plang_write_file_char_w(PascalFile *F, int8_t V, int64_t W) {
    abortIfClosed(F,"write");
    if (W == 0) return;
    trapOnWrongDirection(F, "write", 1);
    if (W < 0) {
        std::fputc(static_cast<unsigned char>(V), F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    std::fprintf(F->Fp, "%*c", checkedWidth(W), static_cast<unsigned char>(V));
    trapOnStreamError(F, "write");
}
void plang_write_file_str_w (PascalFile *F, const char *S, int64_t W)
    { abortIfClosed(F,"write"); writePadded(F, S, W); }

// EP §6.9.3.6: a complex is written as a parenthesized pair of reals — in the
// representation reals are written in, which is why each half goes through the
// real writer rather than being formatted alongside the parentheses.
void plang_write_file_cplx (PascalFile *F, double Re, double Im) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    std::fputc('(', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64(F, Re);
    std::fputc(',', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64(F, Im);
    std::fputc(')', F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_cplx_w(PascalFile *F, double Re, double Im,
                             int64_t W, int64_t D) {
    abortIfClosed(F,"write");
    trapOnWrongDirection(F, "write", 1);
    // plang_write_file_f64_f already picks between "%*.*f" and the
    // exponential fallback on D's sign, which used to be duplicated here.
    std::fputc('(', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64_f(F, Re, W, D);
    std::fputc(',', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64_f(F, Im, W, D);
    std::fputc(')', F->Fp);
    trapOnStreamError(F, "write");
}

// ---- binary typed-file I/O (EP §6.4.3.6 / §6.7.5.2) ----

void plang_read_binary(PascalFile *F, void *Buf, int64_t ElemSize) {
    abortIfClosed(F, "read");
    // Zeroed before the attempt, not just on a failure path: a short fread
    // (whether from real end-of-file or, per issue #124, a file left open
    // write-only) must not leave Buf holding whatever the caller's memory
    // held before the call.
    if (ElemSize > 0) std::memset(Buf, 0, static_cast<std::size_t>(ElemSize));
    trapOnWrongDirection(F, "read", 0);
    std::fread(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "read");
    // eof reads the window, which the fread just invalidated.
    prime(F);
    unloadComponent(F);
}

void plang_write_binary(PascalFile *F, const void *Buf, int64_t ElemSize) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    std::fwrite(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "write");
    unloadComponent(F);
}

// ---- EP §6.7.5.2: extend / update ----

void plang_extend(PascalFile *F, const char *Name) {
    if (!Name || Name[0] == '\0') {
        // Internal file: seek to end of existing temp storage.
        if (F->Fp) {
            std::fflush(F->Fp);
            std::fseek(F->Fp, 0, SEEK_END);
        } else {
            F->Fp      = openTemp("extend");
            F->Binding = PlangBindTemp;
        }
    } else {
        closeStream(F);
        // Open for read+append; create if absent.
        F->Fp = std::fopen(Name, "r+b");
        if (!F->Fp) {
            F->Fp = std::fopen(Name, "w+b");
            if (!F->Fp) {
                char Msg[512];
                std::snprintf(Msg, sizeof(Msg), "cannot open '%s' for extend", Name);
                plang_err_cannot_open(Msg);
            }
        }
        std::fseek(F->Fp, 0, SEEK_END);
    }
    F->Buf      = PlangFileUninit;
    // 2, not 1: extend opens both directions (named files get "r+b" above,
    // same as update), and trapOnWrongDirection treats 2 as the one value
    // that is exempt from its internal-file direction check.
    F->Readable = 2;
    unloadComponent(F);
}

void plang_update(PascalFile *F, const char *Name) {
    if (!Name || Name[0] == '\0') {
        // Internal file: reposition to start without truncating.
        if (F->Fp) {
            std::fflush(F->Fp);
            std::rewind(F->Fp);
        } else {
            F->Fp      = openTemp("update");
            F->Binding = PlangBindTemp;
        }
    } else {
        closeStream(F);
        // Open for read+write; create if absent.
        F->Fp = std::fopen(Name, "r+b");
        if (!F->Fp) {
            F->Fp = std::fopen(Name, "w+b");
            if (!F->Fp) {
                char Msg[512];
                std::snprintf(Msg, sizeof(Msg), "cannot open '%s' for update", Name);
                plang_err_cannot_open(Msg);
            }
        }
        std::rewind(F->Fp);
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 2; // both directions -- see the same note in plang_extend
    unloadComponent(F);
}

// ---- EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate ----
//
// n is a value of the file's declared INDEX TYPE, not a byte offset and not
// a 0-based component count -- ISO §6.7.5.2's own pre-assertion measures
// "ord(n)-ord(a)", a of type T being the index type's smallest value.  Only
// the difference is a component count; n itself never was one except for
// the common case where a happens to be 0.
//
// That computed offset is never range-checked before it reaches fseek, so a
// value the C library cannot honor (behind the index type's origin, most
// directly, which computes a negative byte offset) must have fseek's own
// failure checked: on failure it leaves the stream positioned exactly where
// it already was, so ignoring the return does not just skip the seek, it
// silently redirects whatever read or write comes next onto that unrelated,
// previously-current component instead (issue #233).

void plang_seekread(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekRead");
    if (std::fseek(F->Fp, (N - IndexLow) * ElemSize, SEEK_SET) != 0)
        plang_err_seek_failed("SeekRead", N);
    F->Readable = 1;
    unloadComponent(F);
    prime(F);
}

void plang_seekwrite(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekWrite");
    if (std::fseek(F->Fp, (N - IndexLow) * ElemSize, SEEK_SET) != 0)
        plang_err_seek_failed("SeekWrite", N);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    unloadComponent(F);
}

void plang_seekupdate(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekUpdate");
    if (std::fseek(F->Fp, (N - IndexLow) * ElemSize, SEEK_SET) != 0)
        plang_err_seek_failed("SeekUpdate", N);
    F->Buf      = PlangFileUninit;
    F->Readable = 2; // both directions -- see the note in plang_extend
    unloadComponent(F);
}

// ---- EP §6.7.6.6: position / LastPosition ----
//
// §6.7.6.6: position(f) = succ(a, length(f.L)), LastPosition(f) =
// succ(a, length(f.L~f.R)-1) -- both relative to a, the index type's
// smallest value, not to zero.

int64_t plang_position(PascalFile *F, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "position");
    long pos = std::ftell(F->Fp);
    if (pos < 0) return IndexLow;
    return IndexLow + (ElemSize > 0 ? (int64_t)(pos / ElemSize) : 0);
}

int64_t plang_lastposition(PascalFile *F, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "LastPosition");
    long saved = std::ftell(F->Fp);
    std::fseek(F->Fp, 0, SEEK_END);
    long end = std::ftell(F->Fp);
    if (saved >= 0) std::fseek(F->Fp, saved, SEEK_SET);
    if (ElemSize <= 0 || end <= 0) return IndexLow - 1;
    return IndexLow + (int64_t)(end / ElemSize) - 1;
}

// ---- EP §6.7.6.5: empty ----

int8_t plang_empty(PascalFile *F, int64_t ElemSize) {
    abortIfClosed(F, "empty");
    long saved = std::ftell(F->Fp);
    std::fseek(F->Fp, 0, SEEK_END);
    long end = std::ftell(F->Fp);
    if (saved >= 0) std::fseek(F->Fp, saved, SEEK_SET);
    (void)ElemSize;
    return (saved >= end) ? 1 : 0;
}

// ---- EP §6.4.3.4 BindingType ----

// PlangBindingType is declared in plang/Basic/RequiredRecordLayouts.h, which
// codegen reads as well: bindingStructType() checks the LLVM type it builds
// against this struct field by field, so the two encodings of one layout can
// no longer drift apart.  A hand-kept "255" used to stand here beside
// PlangMaxBindingName's own -- now the one capacity both sides read.

// Binding name table — a small fixed array avoids C++ stdlib dependency.
// Pascal programs rarely open more than a handful of files.
#define PLANG_MAX_BINDINGS 64
#define PLANG_MAX_NAME_LEN 512

static struct {
    PascalFile* file;
    char        name[PLANG_MAX_NAME_LEN];
    int         active;
} BindingTable[PLANG_MAX_BINDINGS];

static void clearBinding(PascalFile *F) {
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i)
        if (BindingTable[i].active && BindingTable[i].file == F)
            BindingTable[i].active = 0;
}

static const char* findBinding(PascalFile *F) {
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i)
        if (BindingTable[i].active && BindingTable[i].file == F)
            return BindingTable[i].name;
    return nullptr;
}

static void setBinding(PascalFile *F, const char *Name) {
    // Update existing entry or use a free slot.
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i) {
        if (BindingTable[i].active && BindingTable[i].file == F) {
            std::strncpy(BindingTable[i].name, Name ? Name : "", PLANG_MAX_NAME_LEN - 1);
            BindingTable[i].name[PLANG_MAX_NAME_LEN - 1] = '\0';
            return;
        }
    }
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i) {
        if (!BindingTable[i].active) {
            BindingTable[i].active = 1;
            BindingTable[i].file   = F;
            std::strncpy(BindingTable[i].name, Name ? Name : "", PLANG_MAX_NAME_LEN - 1);
            BindingTable[i].name[PLANG_MAX_NAME_LEN - 1] = '\0';
            return;
        }
    }
    // plang_close never clears a slot (only an explicit unbind/rebind does,
    // via clearBinding), so a program that binds 65 distinct file variables
    // and never unbinds any of them genuinely exhausts this table.
    plang_err_binding_table_full();
}

// ---- EP §6.7.5.6: bind / unbind ----

/// EP §6.7.5.6: associates F with the external entity named by B.name.  A
/// following reset or rewrite opens that path; see openBound.
void plang_bind(PascalFile *F, const PlangBindingType *B) {
    if (!F) return;
    // "It shall be a dynamic-violation if the variable is already bound to
    // an external entity" -- checked before touching the table, so a repeat
    // bind() is reported rather than silently replacing the first one.
    if (findBinding(F)) plang_err_bind_already_bound();
    clearBinding(F);
    if (!B) return;
    int64_t N = B->name.len;
    if (N < 0) N = 0;
    if (N > PlangMaxBindingName) N = PlangMaxBindingName;
    if (N == 0) return;   // an empty name binds nothing (§6.7.5.6)
    char Name[PlangMaxBindingName + 1];
    std::memcpy(Name, B->name.data, static_cast<size_t>(N));
    Name[N] = '\0';
    setBinding(F, Name);
}

void plang_unbind(PascalFile *F) {
    if (!F) return;
    clearBinding(F);
}

// ---- EP §6.7.6.8: binding ----

void plang_binding(PascalFile *F, PlangBindingType *Out) {
    if (!Out) return;
    Out->bound    = 0;
    Out->name.len = 0;
    if (!F) return;
    // The name survives even before the file is opened, so a program can read
    // back what it bound.
    if (const char* Name = findBinding(F)) {
        auto N = static_cast<int64_t>(std::strlen(Name));
        if (N > PlangMaxBindingName) N = PlangMaxBindingName;
        std::memcpy(Out->name.data, Name, static_cast<size_t>(N));
        Out->name.len = N;
        if (F->Fp) Out->bound = 1;
    }
    // Standard streams (input/output program parameters) are always bound.
    if (F->Fp && F->Binding == PlangBindStd) Out->bound = 1;
}

} // extern "C"

} // namespace plang
