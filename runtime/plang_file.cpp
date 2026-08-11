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

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// ---- open / close ----

static void closeStream(PascalFile *F) {
    if (F->Fp && F->Fp != stdin && F->Fp != stdout) std::fclose(F->Fp);
    F->Fp      = nullptr;
    F->Binding = PlangBindNone;
}

static std::FILE *openTemp(const char *Op) {
    std::FILE *Fp = std::tmpfile();
    if (!Fp) {
        std::fprintf(stderr, "plang runtime: cannot create temporary file in '%s'\n", Op);
        std::abort();
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
    // EP §6.7.5.6: a file bound to an external entity opens that entity even
    // when reset is called without an explicit name.
    if (!Name || Name[0] == '\0') Name = findBinding(F);
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
            std::fprintf(stderr, "plang runtime: cannot open '%s' for reading\n", Name);
            std::abort();
        }
    }
    F->Readable = 1;
    unloadComponent(F);
    prime(F);
}

void plang_rewrite(PascalFile *F, const char *Name, int8_t /*IsText*/) {
    if (!Name || Name[0] == '\0') Name = findBinding(F);
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
            std::fprintf(stderr, "plang runtime: cannot open '%s' for writing\n", Name);
            std::abort();
        }
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
    if (ElemSize < 1) ElemSize = 1;
    std::fwrite(F->Comp, static_cast<std::size_t>(ElemSize), 1, F->Fp);
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
    std::fputc('\n', F->Fp);
}

void plang_page_file(PascalFile *F) {
    abortIfClosed(F, "page");
    std::fputc('\f', F->Fp);
}

// ---- typed read (text file) ----

void plang_read_file_i64(PascalFile *F, int64_t *P) {
    abortIfClosed(F, "read");
    std::fscanf(F->Fp, "%" SCNd64, P);
    prime(F);
    unloadComponent(F);
}

void plang_read_file_f64(PascalFile *F, double *P) {
    abortIfClosed(F, "read");
    std::fscanf(F->Fp, "%lf", P);
    prime(F);
    unloadComponent(F);
}

void plang_read_file_char(PascalFile *F, int8_t *P) {
    abortIfClosed(F, "read");
    ensurePrimed(F);
    if (F->Buf == EOF) { *P = 0; return; }
    // §6.9.1 reads a char as `v := f^; get(f)`, and §6.4.3.5 gives f^ the value
    // of a space at a line marker: the marker separates the lines rather than
    // belonging to one, so reading a file character by character yields a space
    // where each line ends and not the newline the line is stored with.
    const int C = advance(F);
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
    ensurePrimed(F);
    while (F->Buf != EOF && F->Buf != '\n') {
        const int C = advance(F);
        if (Len < Cap) Data[Len++] = static_cast<char>(C);
    }
    *reinterpret_cast<int64_t*>(Base) = Len;
}

/// Writes the string(N) at S, which is not null-terminated.
void plang_str_write_file(PascalFile *F, const void *S, int64_t /*Cap*/) {
    abortIfClosed(F, "write");
    const auto* Base = static_cast<const char*>(S);
    const int64_t Len = *reinterpret_cast<const int64_t*>(Base);
    if (Len > 0)
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
}

/// The same, in a field of W characters: right-justified, and truncated rather
/// than widened when it does not fit (§6.9.3.6).
void plang_str_write_file_w(PascalFile *F, const void *S, int64_t /*Cap*/,
                            int64_t W) {
    abortIfClosed(F, "write");
    if (W <= 0) return;
    const auto* Base = static_cast<const char*>(S);
    int64_t Len = *reinterpret_cast<const int64_t*>(Base);
    if (Len < 0) Len = 0;
    for (int64_t I = Len; I < W; ++I) std::fputc(' ', F->Fp);
    if (Len > W) Len = W;
    if (Len > 0)
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
}

// ---- typed write (text file) ----

// Defined below with the rest of the field-width forms; the real writer with no
// width is the same one with the default.
void plang_write_file_f64_e(PascalFile *F, double V, int64_t W);

void plang_write_file_i64 (PascalFile *F, int64_t     V) { abortIfClosed(F,"write"); std::fprintf(F->Fp, "%" PRId64, V); }
void plang_write_file_f64 (PascalFile *F, double      V) { plang_write_file_f64_e(F, V, PlangRealWidth); }
void plang_write_file_bool(PascalFile *F, int8_t      V) { abortIfClosed(F,"write"); std::fputs(V ? "true" : "false", F->Fp); }
void plang_write_file_char(PascalFile *F, int8_t      V) { abortIfClosed(F,"write"); std::fputc(static_cast<unsigned char>(V), F->Fp); }
void plang_write_file_str (PascalFile *F, const char *S) { abortIfClosed(F,"write"); std::fputs(S ? S : "", F->Fp); }

// ---- typed write with a field width (ISO §6.9.3.1) ----
//
// Same formats as the stdout writers in plang_io.cpp.  A width of zero writes
// nothing for the fixed-size forms and no padding for the numeric ones, which
// is what a printf width of zero already does.

void plang_write_file_i64_w (PascalFile *F, int64_t V, int64_t W)
    { abortIfClosed(F,"write"); std::fprintf(F->Fp, "%*" PRId64, static_cast<int>(W), V); }
void plang_write_file_f64_e (PascalFile *F, double V, int64_t W) {
    abortIfClosed(F, "write");
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W);
    std::fwrite(Buf, 1, N, F->Fp);
}
void plang_write_file_f64_f (PascalFile *F, double V, int64_t W, int64_t D)
    { abortIfClosed(F,"write"); std::fprintf(F->Fp, "%*.*f", static_cast<int>(W), static_cast<int>(D), V); }
// §6.9.3.6: the field is exactly W characters, so a longer string loses its
// tail; the `%*s` a width otherwise maps onto pads but never truncates.
// §6.9.3.5 writes a boolean as its char-string would be written, truncation
// included.
static void writePadded(PascalFile *F, const char *S, int64_t W) {
    if (W <= 0) return;
    const auto Width = static_cast<std::size_t>(W);
    const std::size_t Len = S ? std::strlen(S) : 0;
    for (std::size_t I = Len; I < Width; ++I) std::fputc(' ', F->Fp);
    if (Len) std::fwrite(S, 1, Len < Width ? Len : Width, F->Fp);
}
void plang_write_file_bool_w(PascalFile *F, int8_t V, int64_t W)
    { abortIfClosed(F,"write"); writePadded(F, V ? "true" : "false", W); }
void plang_write_file_char_w(PascalFile *F, int8_t V, int64_t W)
    { abortIfClosed(F,"write"); if (W != 0) std::fprintf(F->Fp, "%*c", static_cast<int>(W), static_cast<unsigned char>(V)); }
void plang_write_file_str_w (PascalFile *F, const char *S, int64_t W)
    { abortIfClosed(F,"write"); writePadded(F, S, W); }

// EP §6.9.3.6: a complex is written as a parenthesized pair of reals — in the
// representation reals are written in, which is why each half goes through the
// real writer rather than being formatted alongside the parentheses.
void plang_write_file_cplx (PascalFile *F, double Re, double Im) {
    abortIfClosed(F, "write");
    std::fputc('(', F->Fp);
    plang_write_file_f64(F, Re);
    std::fputc(',', F->Fp);
    plang_write_file_f64(F, Im);
    std::fputc(')', F->Fp);
}
void plang_write_file_cplx_w(PascalFile *F, double Re, double Im,
                             int64_t W, int64_t D) {
    abortIfClosed(F,"write");
    std::fputc('(', F->Fp);
    if (D >= 0) std::fprintf(F->Fp, "%*.*f", static_cast<int>(W),
                             static_cast<int>(D), Re);
    else        plang_write_file_f64_e(F, Re, W);
    std::fputc(',', F->Fp);
    if (D >= 0) std::fprintf(F->Fp, "%*.*f", static_cast<int>(W),
                             static_cast<int>(D), Im);
    else        plang_write_file_f64_e(F, Im, W);
    std::fputc(')', F->Fp);
}

// ---- binary typed-file I/O (EP §6.4.3.6 / §6.7.5.2) ----

void plang_read_binary(PascalFile *F, void *Buf, int64_t ElemSize) {
    abortIfClosed(F, "read");
    std::fread(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    // eof reads the window, which the fread just invalidated.
    prime(F);
    unloadComponent(F);
}

void plang_write_binary(PascalFile *F, const void *Buf, int64_t ElemSize) {
    abortIfClosed(F, "write");
    std::fwrite(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
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
                std::fprintf(stderr, "plang runtime: cannot open '%s' for extend\n", Name);
                std::abort();
            }
        }
        std::fseek(F->Fp, 0, SEEK_END);
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 1;
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
                std::fprintf(stderr, "plang runtime: cannot open '%s' for update\n", Name);
                std::abort();
            }
        }
        std::rewind(F->Fp);
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 1;
    unloadComponent(F);
}

// ---- EP §6.7.5.2: SeekRead / SeekWrite / SeekUpdate ----

void plang_seekread(PascalFile *F, int64_t N, int64_t ElemSize) {
    abortIfClosed(F, "SeekRead");
    std::fseek(F->Fp, N * ElemSize, SEEK_SET);
    F->Readable = 1;
    unloadComponent(F);
    prime(F);
}

void plang_seekwrite(PascalFile *F, int64_t N, int64_t ElemSize) {
    abortIfClosed(F, "SeekWrite");
    std::fseek(F->Fp, N * ElemSize, SEEK_SET);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    unloadComponent(F);
}

void plang_seekupdate(PascalFile *F, int64_t N, int64_t ElemSize) {
    abortIfClosed(F, "SeekUpdate");
    std::fseek(F->Fp, N * ElemSize, SEEK_SET);
    F->Buf      = PlangFileUninit;
    F->Readable = 1;
    unloadComponent(F);
}

// ---- EP §6.7.6.6: position / LastPosition ----

int64_t plang_position(PascalFile *F, int64_t ElemSize) {
    abortIfClosed(F, "position");
    long pos = std::ftell(F->Fp);
    if (pos < 0) return 0;
    return ElemSize > 0 ? (int64_t)(pos / ElemSize) : 0;
}

int64_t plang_lastposition(PascalFile *F, int64_t ElemSize) {
    abortIfClosed(F, "LastPosition");
    long saved = std::ftell(F->Fp);
    std::fseek(F->Fp, 0, SEEK_END);
    long end = std::ftell(F->Fp);
    if (saved >= 0) std::fseek(F->Fp, saved, SEEK_SET);
    if (ElemSize <= 0 || end <= 0) return -1;
    return (int64_t)(end / ElemSize) - 1;
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

/// Layout must match the LLVM struct generated by bindingStructType():
/// { { i64, [255 x i8] } name, i8 bound }.  EP §6.4.3.4 requires both fields,
/// with the string capacity left to the implementation.
#define PLANG_BINDING_NAME_CAP 255

struct PlangBindingType {
    struct {
        int64_t len;
        char    data[PLANG_BINDING_NAME_CAP];
    } name;
    int8_t bound;
};

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
    // Table full — silently ignore.
}

// ---- EP §6.7.5.6: bind / unbind ----

/// EP §6.7.5.6: associates F with the external entity named by B.name.  A
/// following reset or rewrite opens that path; see openBound.
void plang_bind(PascalFile *F, const PlangBindingType *B) {
    if (!F) return;
    clearBinding(F);
    if (!B) return;
    int64_t N = B->name.len;
    if (N < 0) N = 0;
    if (N > PLANG_BINDING_NAME_CAP) N = PLANG_BINDING_NAME_CAP;
    if (N == 0) return;   // an empty name binds nothing (§6.7.5.6)
    char Name[PLANG_BINDING_NAME_CAP + 1];
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
        if (N > PLANG_BINDING_NAME_CAP) N = PLANG_BINDING_NAME_CAP;
        std::memcpy(Out->name.data, Name, static_cast<size_t>(N));
        Out->name.len = N;
        if (F->Fp) Out->bound = 1;
    }
    // Standard streams (input/output program parameters) are always bound.
    if (F->Fp && F->Binding == PlangBindStd) Out->bound = 1;
}

} // extern "C"

} // namespace plang
