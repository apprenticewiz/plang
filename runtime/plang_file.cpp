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

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h> // ftruncate (plang_tp_truncate) -- no higher-level libc call exists

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

/// Defined with the write-path table further down (see closeFinalLine): the
/// path a file currently open for writing by name was opened with, so a
/// later reset/rewrite/close/extend/update on the same file variable can
/// finish its last line before the write-only stream that produced it is
/// abandoned or reused for something else.
static const char* findWritePath(PascalFile *F);
static void setWritePath(PascalFile *F, const char *Name);
static void clearWritePath(PascalFile *F);

/// Defined with the other field-width writers further down (ISO §6.9.3.1);
/// plang_str_write_file_w's string(N) writer sits earlier in the file and
/// needs it to bound its own hand-rolled padding loop the same way the
/// writers below it already do (issue #247).
static int checkedWidth(int64_t W);

/// Defined with the binding table further down; reset and rewrite call this
/// too now (issue #239): a name given directly to either -- not just one
/// given through bind -- is retained the same way, so a later call with no
/// name reopens that same external entity instead of diverting to fresh,
/// unnamed internal storage.
static void setBinding(PascalFile *F, const char *Name);

/// Defined with SeekRead/SeekWrite/SeekUpdate further down; plang_tp_seek
/// (Cluster C item 6) computes an identical N/ElemSize/IndexLow byte offset,
/// with the identical overflow safety, and reuses this rather than
/// duplicating it -- IndexLow is always 0 for plang_tp_seek's own call
/// (Turbo's Seek is 0-relative, with no EP index-type origin to offset by).
static bool seekOffset(int64_t N, int64_t ElemSize, int64_t IndexLow,
                        long *Offset);

/// Defined with the other runtime error reporters in plang_sys.cpp.
[[noreturn]] void plang_err_bind_already_bound(void);
[[noreturn]] void plang_err_binding_table_full(void);
[[noreturn]] void plang_err_cannot_open(const char *Msg);
[[noreturn]] void plang_err_field_width(int64_t W);
[[noreturn]] void plang_err_file_wrong_mode(const char *Op);
[[noreturn]] void plang_err_seek_failed(const char *Op, int64_t N);
[[noreturn]] void plang_err_read_format(const char *Op);
[[noreturn]] void plang_err_read_int_range(const char *Op, const char *Tok);
/// Turbo's own numbered run-time error reporter -- see plang_io.cpp's own
/// comment on this (shared with plang_sys.cpp's RangeCheckGuards precedent)
/// for why plang_read_file_i64_turbo/plang_read_file_f64_turbo below call it
/// directly instead of going through a CodeGen-emitted guard.
[[noreturn]] void plang_tp_runerror(int64_t Code);

/// -std=turbo only: the single InOutRes global -- DEFINED once in
/// runtime/plang_sys.cpp, right beside plang_tp_exitcode/plang_tp_randseed/
/// plang_tp_filemode, whose own "one definition, every compiled object only
/// declares" mechanism this reuses exactly (Sema::registerBuiltins' InOutRes
/// Symbol, CodeGenProcs.cpp's emitPredefinedGlobals).  Declared here with a
/// plain `extern` -- not through that LLVM-global machinery -- because every
/// USE in this file is from the runtime's OWN C++ code (tpFileReady,
/// plang_eof_file_turbo/plang_eoln_file_turbo, and every other `_turbo`
/// entry point below that can set or test it), never from CodeGen-emitted
/// Pascal IR.  See plang_sys.cpp's own definition for why this is
/// int64_t -- deliberately NOT Borland's 16-bit Word.
extern int64_t plang_tp_inoutres;

/// -std=turbo only: sets InOutRes to \p Code, but ONLY when InOutRes does
/// not already hold a pending, unread error.  Every site in this file that
/// sets InOutRes on a failure goes through this (tpFileReady itself, and
/// Reset/Rewrite/Append's own fopen-failure and EISDIR arms) rather than
/// assigning the global directly, so this rule cannot be accidentally
/// reintroduced by a future call site that assigns bypassing it.
///
/// This is not a defensive nicety -- it is REQUIRED for this item's own
/// "deferred, position-keyed checking" to match real Borland/FPC field
/// practice, and was FOUND, not guessed at, by testing the local `fpc
/// -Mtp` install against this item's own manual-testing requirement:
/// `{$I-} Reset(f); {$I+} Read(f, x);` with Reset failing on a file that
/// does not exist (InOutRes 2).  Naively, Read's own tpFileReady call would
/// find F still closed and set 103 ("file not open"), and the checked
/// position right after Read would then report 103 -- plausible-sounding,
/// but empirically WRONG: `fpc -Mtp` reports 2, Reset's own original code,
/// at that checkpoint, not 103.  Confirmed with several further probes
/// (`fpc -Mtp`, this file's own test fixtures, not this project's Pascal):
/// a second, differently-failing Reset does not overwrite a pending code
/// with its own either (still the first code); a `Write` against the same
/// still-closed file behaves identically to `Read`.  Only an explicit
/// `IOResult` call (which clears InOutRes as it reads it -- plang_tp_
/// ioresult, runtime/plang_sys.cpp) lets the NEXT failure's own code start
/// being recorded again.  Getting this backwards is not merely
/// differently-worded -- since a checked I/O failure's exit status IS the
/// InOutRes code itself (plang_iocheck, runtime/plang_sys.cpp), the WRONG
/// exit status would ship.
///
/// Real field practice goes further still: `fpc -Mtp` shows an unrelated,
/// otherwise-successful operation (even a plain console `Writeln`, or a
/// SECOND Reset against a perfectly valid, different file) is ALSO
/// silently skipped for as long as an error remains pending and unread --
/// not merely prevented from overwriting InOutRes's CODE, but seemingly not
/// attempted at all.  Reproducing that full latch is a considerably larger
/// change than this one helper gives for free (Reset/Rewrite/Append's own
/// fopen calls, guarded by this same helper just below, still ATTEMPT the
/// open rather than skip it outright, so a Reset against a valid file can
/// still genuinely succeed even while an earlier failure sits unread) --
/// this fix's own scope is "do not misreport which error is pending", not
/// "suppress every operation while one is"; the latter is future work this
/// item deliberately leaves for whoever next touches this area to pick up.
static void setInOutResIfClear(int64_t Code) {
    if (plang_tp_inoutres == 0) plang_tp_inoutres = Code;
}

/// -std=turbo only: maps a POSIX errno to the InOutRes code real Turbo
/// Pascal / Free Pascal field practice actually reports for it -- confirmed
/// against the locally installed `fpc` 3.2.2's own
/// rtl/linux/sysos.inc (PosixToRunError/Errno2InoutRes), not guessed at.
/// That table: ENOENT -> 2 ("file not found"), ENAMETOOLONG -> 3 ("path not
/// found"), ENFILE/EMFILE -> 4 ("too many open files"),
/// EACCES/EROFS/EEXIST/ENOTEMPTY/EBUSY/ENOTDIR/EISDIR -> 5 ("file access
/// denied" -- FPC folds all seven into the one code, not just EACCES), and
/// EPIPE/EINTR/EIO/EAGAIN/ENOSPC -> 101 ("disk write error"), and (added by
/// Cluster C item 6, BlockRead/BlockWrite/Seek/...: plang_tp_seek's own
/// `Seek(f, -5)` on a fresh `Rewrite`-opened file is an exercised call site
/// now, empirically confirmed with `fpc -Mtp` to report 218, not the raw
/// EINVAL value) EINVAL -> 218.  Real FPC's table also carries ENOMEM/
/// EFAULT -> 217 and EBADF -> 6; still left out here because nothing in
/// this item calls this with an errno a POSIX fopen(3)/fseek(3) can
/// actually produce either of those two for, and adding them with no
/// exercised call site to confirm them against would be guessing rather
/// than matching field practice.  EPERM is deliberately
/// NOT folded into EACCES's 5, tempting as "both mean permission" looks --
/// FPC's own `case` statement has no ESysEPERM arm either, so it falls to
/// the same `else` every unlisted errno does below: passed through
/// unchanged, exactly like real `fpc -Mtp`.  This is also deliberately NOT
/// the whole InOutRes table: 100 (disk read error/EOF), 102 (file not
/// assigned), 103 (file not open), 104/105 (not open for input/output) and
/// 106 (invalid numeric format) are never DERIVED from an errno in real FPC
/// either -- its own I/O layer sets each directly as a literal constant at
/// the point of failure, which is exactly what tpFileReady's own literal
/// 103 below, and plang_tp_runerror(106)'s existing Turbo numeric-parse
/// callers, already do -- so producing any of those five is not this
/// function's job.
int64_t plang_tp_posix_to_run_error(int PosixErrno) {
    switch (PosixErrno) {
        case ENOENT:        return 2;
        case ENAMETOOLONG:  return 3;
        case ENFILE:
        case EMFILE:        return 4;
        case EACCES:
        case EROFS:
        case EEXIST:
        case ENOTEMPTY:
        case EBUSY:
        case ENOTDIR:
        case EISDIR:        return 5;
        case EPIPE:
        case EINTR:
        case EIO:
        case EAGAIN:
        case ENOSPC:        return 101;
        case EINVAL:        return 218;
        default:            return PosixErrno;
    }
}

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

/// Every operation below calls this first, which makes it the one place to
/// clear the C stream's error indicator before the operation does anything.
/// ferror() is sticky -- it stays set until clearerr() runs, not just for
/// the one call that tripped it -- so without this, a call that trips it
/// without checking (get/prime's own lookahead read on a write-only stream,
/// itself not a checked operation) leaves it set for trapOnStreamError to
/// find on some later, unrelated, and genuinely successful operation and
/// misattribute to that instead (issue #238). Clearing here rather than
/// after keeps every check that follows -- trapOnStreamError's ferror,
/// trapOnScanError's feof -- answering for this call alone: whatever an
/// earlier one left behind cannot survive to be misread as this one's own.
static void abortIfClosed(PascalFile *F, const char *Op) {
    if (!F || !F->Fp) {
        std::fprintf(stderr, "plang runtime: file not open in '%s'\n", Op);
        std::abort();
    }
    std::clearerr(F->Fp);
}

/// -std=turbo only: the non-aborting counterpart to abortIfClosed just
/// above, for every file-I/O entry point Turbo-generated code can actually
/// reach.  abortIfClosed itself is UNCHANGED -- see its own comment -- and
/// still aborts unconditionally at every ISO/EP call site, which is correct
/// ISO/EP behavior a later {$I+}/InOutRes item does not get to relax.  This
/// project's P7 rule (see e.g. plang_tp_reset/plang_tp_rewrite/
/// plang_tp_append/plang_tp_close's own top comment, and
/// plang_sys.cpp's "-std=turbo run-time error reporting" section) is that
/// dialect selection happens at CODEGEN TIME, through WHICH SYMBOL codegen
/// calls, never inside one runtime function branching on a passed-in "which
/// dialect" flag -- an ISO object file and a Turbo one can be linked into
/// the same program, so a runtime function can never ask "which dialect is
/// this" at all.  Every `_turbo`-suffixed sibling below (and the ones
/// already established by PR #478/plang_read_file_i64_turbo before this
/// item) calls THIS instead of abortIfClosed, and is reached only when
/// CodeGen (CGProcCall.cpp/BuiltinIO.cpp/CGFuncCall.cpp) already knows,
/// statically, that it is emitting Turbo code.
///
/// On success (F && F->Fp): the identical clearerr() abortIfClosed itself
/// does, so a Turbo entry point gets the exact same sticky-ferror()
/// protection issue #238 fixed for the ISO ones -- returns true.  On
/// failure: sets InOutRes to 103 ("file not open", Borland/FPC's own
/// documented code for exactly this condition -- see plang_tp_
/// posix_to_run_error's own comment for why this is a literal here and not
/// something that function computes) THROUGH setInOutResIfClear, not a
/// direct assignment -- see that function's own comment for why a pending,
/// unread InOutRes must survive a later failing operation unchanged (this
/// item's own manual-testing requirement, `{$I-} Reset(f); {$I+} Read(f,
/// x);` with Reset failing, is exactly the case that would misreport 103
/// instead of Reset's own original code without this) -- and returns
/// false.  Every caller below is written to immediately `return` (or, for a
/// Func, return a harmless default) when this comes back false, performing
/// NONE of its own I/O -- so a closed file's operation becomes a silent
/// no-op instead of a crash.  \p Op is accepted only for symmetry with
/// abortIfClosed's own signature (every call site already has one to
/// hand); nothing here prints it, since there is nothing to print -- the
/// failure is recorded in InOutRes, not on stderr.
static bool tpFileReady(PascalFile *F, const char * /*Op*/) {
    if (!F || !F->Fp) {
        setInOutResIfClear(103);
        return false;
    }
    std::clearerr(F->Fp);
    return true;
}

/// ISO §6.7.5.6: a write/read against a file positioned or opened for the
/// other direction fails at the C stream level -- fread/fwrite return short,
/// fprintf/fputc/fputs return a negative/EOF sentinel, and in every one of
/// those cases the stream's own error indicator is set.  A stdio call that
/// merely hit real end-of-file sets neither -- so checking ferror here
/// (rather than treating any short return as this violation) traps exactly
/// the wrong-mode case and leaves the eof and malformed-input cases exactly
/// as they behaved before.  F->Readable is not a substitute for this:
/// seekread/seekupdate set it without reopening F->Fp, so it does not
/// reflect the mode the stream was actually opened in (the gap issue #124
/// was filed against).
static void trapOnStreamError(PascalFile *F, const char *Op) {
    if (std::ferror(F->Fp)) plang_err_file_wrong_mode(Op);
}

/// Issue #152: extends the #124 mode trap to an internal (unbound) file.
/// Such a file is backed by tmpfile(), which glibc always opens "w+b" --
/// genuinely bidirectional at the C level -- regardless of which way the
/// Pascal file is currently facing, so a wrong-direction stdio call there
/// succeeds outright: ferror/feof never fires, and trapOnStreamError has
/// nothing to see.  F->Readable is the only place the intended direction is
/// recorded for such a file (set by reset/rewrite and
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

// Shared by the binding table (further down) and the write-path table right
// below: a bound name and a currently-open write path are the same shape of
// entry, one small fixed array apiece, and Pascal programs rarely have more
// than a handful of files open at once.
#define PLANG_MAX_BINDINGS 64
#define PLANG_MAX_NAME_LEN 512

/// Remembers, for a file currently open for writing by name, the path it was
/// opened with.  closeFinalLine (below) needs this: such a file is opened
/// "w" -- write-only, so that a stray Pascal-level read against it is caught
/// at the C level rather than silently succeeding (issue #124) -- and a
/// write-only stream cannot be read back through itself to see what its last
/// byte was.  An internal (tmpfile()-backed) file gets no entry here: it is
/// always opened "w+b" and can check its own tail directly, no second stream
/// required.
static struct {
    PascalFile *File;
    char        Path[PLANG_MAX_NAME_LEN];
    int         Active;
} WritePathTable[PLANG_MAX_BINDINGS];

static const char* findWritePath(PascalFile *F) {
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i)
        if (WritePathTable[i].Active && WritePathTable[i].File == F)
            return WritePathTable[i].Path;
    return nullptr;
}

static void clearWritePath(PascalFile *F) {
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i)
        if (WritePathTable[i].Active && WritePathTable[i].File == F)
            WritePathTable[i].Active = 0;
}

/// Replaces whatever entry F had (if any) with Name.  A null or empty Name
/// just clears the entry: plang_extend/plang_update fall back to it when
/// their own Name argument is empty, and an internal file needs none.
static void setWritePath(PascalFile *F, const char *Name) {
    clearWritePath(F);
    if (!Name || Name[0] == '\0') return;
    for (int i = 0; i < PLANG_MAX_BINDINGS; ++i) {
        if (!WritePathTable[i].Active) {
            WritePathTable[i].Active = 1;
            WritePathTable[i].File   = F;
            std::strncpy(WritePathTable[i].Path, Name, PLANG_MAX_NAME_LEN - 1);
            WritePathTable[i].Path[PLANG_MAX_NAME_LEN - 1] = '\0';
            return;
        }
    }
    // Table full: closeFinalLine's caller-facing behavior degrades to what
    // it was before this table existed (a missed finalization on a
    // write-only stream, not a crash) rather than aborting the program over
    // a bookkeeping shortage.
}

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
///
/// A named file is opened "w" (see WritePathTable's comment above), and
/// fgetc on a write-only stream fails outright -- glibc and friends refuse
/// even to try, returning EOF -- which this function used to read exactly
/// like "the file already ends in a marker" and so silently do nothing:
/// dead code for every named file, only ever exercised by an internal
/// (tmpfile()-backed) file's always-bidirectional "w+b" stream.  Where
/// WritePathTable has a path for F, this checks the last byte through a
/// second, independent read-only stream on that same path instead -- a
/// different FILE* entirely, so there is no read immediately followed by a
/// write on one stream to worry about -- and still writes the fix, if any,
/// out through the original F->Fp exactly as before.
static void closeFinalLine(PascalFile *F) {
    if (!F->Fp || F->Readable) return;
    std::fflush(F->Fp);
    const long End = std::ftell(F->Fp);
    if (End <= 0) return;                 // nothing written: no line to close

    if (const char *Path = findWritePath(F)) {
        std::FILE *Peek = std::fopen(Path, "rb");
        if (!Peek) return;
        int Last = EOF;
        if (std::fseek(Peek, -1, SEEK_END) == 0) Last = std::fgetc(Peek);
        std::fclose(Peek);
        if (Last != '\n' && Last != EOF) std::fputc('\n', F->Fp);
        return;
    }

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

void plang_rewrite(PascalFile *F, const char *Name, int8_t IsText) {
    // §6.4.3.5 makes a text file a sequence of lines, each ended by a line
    // marker: starting a fresh rewrite abandons whatever F was writing
    // before, and that has to be finished first, the same as turning it
    // around to read (plang_reset) already does.
    if (IsText) closeFinalLine(F);
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
        clearWritePath(F);
    } else {
        F->Fp = std::fopen(Name, "w");
        if (!F->Fp) {
            char Msg[512];
            std::snprintf(Msg, sizeof(Msg), "cannot open '%s' for writing", Name);
            plang_err_cannot_open(Msg);
        }
        setWritePath(F, Name);
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

void plang_close(PascalFile *F, int8_t IsText) {
    // §6.4.3.5: closing a file being written has to finish its last line
    // first (issue #234) -- this was the one caller closeFinalLine never
    // had, so a program that wrote a partial line and just called close,
    // with no intervening reset, got no finalization at all.
    if (IsText) closeFinalLine(F);
    closeStream(F);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    std::free(F->Comp);
    F->Comp     = nullptr;
    F->CompSize = 0;
    unloadComponent(F);
    clearWritePath(F);
}

// ---- -std=turbo only: Assign / Reset / Rewrite / Append / Close ----
//
// TP's own file model, which REPLACES (not extends) everything above: real
// Assign(f, name) records a filename on f itself (PascalFile.Name, added
// for exactly this -- see PascalFileLayout.h), and Reset/Rewrite/Append
// then open whatever that name says, with no filename argument of their
// own.  This is deliberately a genuinely SEPARATE function family from
// plang_reset/plang_rewrite/plang_close above, not those same functions
// with a dialect flag -- CGProcCall.cpp's dispatch picks one family or the
// other at the CALL SITE (this project's P7 rule), because an ISO object
// file and a Turbo one can be linked into a single program and each must
// keep its own file model.
//
// Three pieces of the ISO functions above are DELIBERATELY NOT carried
// over here, each for its own reason:
//  - closeFinalLine (ISO §6.4.3.5's "a written text file's last line is
//    finished before it is turned around or closed"): not a TP rule at
//    all -- confirmed against `fpc -Mtp`, which leaves a partial last line
//    exactly as the program wrote it.
//  - findBinding/setBinding (EP §6.7.5.6's separate bind() table, with its
//    "retain an explicit name" fallback, issue #239): TP has no bind() and
//    no such fallback -- the ONLY place a name comes from is F->Name,
//    directly on the struct, set by Assign and nothing else.
//  - prime(F)'s unconditional call at the end of plang_reset: §6.9.1 makes
//    ISO's f^ defined immediately after reset, which forces that eager
//    read.  TP has no such buffer-variable guarantee, and eagerly priming
//    a console-bound Reset would block a program that never actually reads
//    it waiting on a keystroke -- the same "`program count(input,output)`
//    must not hang before its first writeln" concern plang_bind_std's own
//    comment raises for ISO's file-parameter binding.  These functions
//    leave F->Buf at PlangFileUninit and let ensurePrimed (used by every
//    status/read entry point already, dialect-agnostically) prime lazily
//    on the first real query instead.
//
// Reset/Rewrite/Append's own open FAILURE (fopen returning null, or Reset's
// directory guard just below) now sets InOutRes via
// plang_tp_posix_to_run_error(errno) and simply RETURNS, leaving F closed --
// F->Fp stays the null closeStream() already left it, so a following I/O
// call against F correctly finds it not-open (tpFileReady's own 103) --
// instead of the ISO functions' plang_err_cannot_open, which aborts the
// whole process.  This is this item's own manual-test requirement made
// concrete: a Reset against a file that does not exist, or one with no read
// permission, has to set a sensible InOutRes rather than crash.  errno is
// captured IMMEDIATELY after the failing fopen, before anything else
// (including std::fclose in Reset's directory-guard arm) has a chance to
// clobber it.  None of Assign/Reset/Rewrite/Append below range-checks
// F->Mode against fmClosed..fmInOut before proceeding, and Close is left
// entirely alone (it cannot itself fail) -- both remain a later item's job,
// exactly as PascalFileLayout.h's own RecSize field comment says.

/// TP Assign(f, name): binds F to an external filename (or, for an empty
/// name, to "the console" -- see the Reset/Rewrite/Append functions below
/// for what that means) with NO check of any kind on F's current state.
/// This is deliberate and must stay true even once a later item adds an
/// "is this file already in an error state" guard to Reset/Rewrite/Append:
/// Assign is precisely the operation that has to work no matter what state
/// F is in, or a program could never recover from one.
void plang_tp_assign(PascalFile *F, const char *Name) {
    const std::size_t Cap = static_cast<std::size_t>(PlangFileNameCap) - 1;
    const std::size_t Len = Name ? std::strlen(Name) : 0;
    const std::size_t N   = Len < Cap ? Len : Cap;
    if (N) std::memcpy(F->Name, Name, N);
    F->Name[N] = '\0';
    F->Mode = PlangFmClosed;
    // F->Fp is deliberately left untouched: real Assign on a file that is
    // currently open is not a case this item builds any behavior for
    // (Assign's own job is only to record a name and mark the file
    // closed); a later {$I+}/InOutRes item is the natural place to decide
    // whether that should itself be an error.
}

/// TP Reset(f): opens F->Name (as bound by an earlier Assign) for reading,
/// or -- for an empty bound name -- attaches F to stdin.  See this
/// section's own top comment for the three ISO behaviors deliberately not
/// reproduced here.
void plang_tp_reset(PascalFile *F) {
    closeStream(F);
    if (F->Name[0] == '\0') {
        F->Fp      = stdin;
        F->Binding = PlangBindStd;
    } else {
        F->Fp = std::fopen(F->Name, "r");
        if (!F->Fp) {
            const int Err = errno; // captured before anything else can clobber it
            setInOutResIfClear(plang_tp_posix_to_run_error(Err));
            return;
        }
        // Issue #287's directory guard, reproduced here for the identical
        // reason plang_reset above has it: opening a directory read-only
        // succeeds at the C level on POSIX with nothing ever readable back.
        // EISDIR is not one of PosixToRunError's own mapped cases (real POSIX
        // fopen("r") never fails on a directory to begin with -- this guard
        // exists precisely because it does NOT), so this reports the same
        // code 5 ("file access denied") real FPC's own EISDIR arm maps to,
        // as the closest honest match rather than inventing a new one.
        struct stat St;
        if (fstat(fileno(F->Fp), &St) == 0 && S_ISDIR(St.st_mode)) {
            std::fclose(F->Fp);
            F->Fp = nullptr;
            setInOutResIfClear(5);
            return;
        }
    }
    F->Buf      = PlangFileUninit; // left unprimed -- see this section's top comment
    F->Readable = 1;
    F->Mode     = PlangFmInput;
    unloadComponent(F);
}

/// TP Rewrite(f): creates/truncates F->Name for writing, or -- for an empty
/// bound name -- attaches F to stdout.
void plang_tp_rewrite(PascalFile *F) {
    closeStream(F);
    if (F->Name[0] == '\0') {
        F->Fp      = stdout;
        F->Binding = PlangBindStd;
    } else {
        F->Fp = std::fopen(F->Name, "w");
        if (!F->Fp) {
            const int Err = errno;
            setInOutResIfClear(plang_tp_posix_to_run_error(Err));
            return;
        }
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    F->Mode     = PlangFmOutput;
    unloadComponent(F);
}

/// TP Append(f): opens F->Name for writing at its current end (creating it
/// if absent), or -- for an empty bound name -- attaches F to stdout.  Real
/// Turbo Pascal sets Mode to fmOutput here too, the same as Rewrite --
/// confirmed against `fpc -Mtp` (TextRec(f).Mode reads identically after
/// either call) -- Append is not a fourth, distinct mode of its own.
void plang_tp_append(PascalFile *F) {
    closeStream(F);
    if (F->Name[0] == '\0') {
        F->Fp      = stdout;
        F->Binding = PlangBindStd;
    } else {
        F->Fp = std::fopen(F->Name, "a");
        if (!F->Fp) {
            const int Err = errno;
            setInOutResIfClear(plang_tp_posix_to_run_error(Err));
            return;
        }
    }
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    F->Mode     = PlangFmOutput;
    unloadComponent(F);
}

/// TP Reset/Rewrite's RecSize wiring (Cluster A item 4): a typed file's
/// RecSize is SizeOf(its element), computed by codegen and always nonzero
/// (Sema's err_file_component_zero_size already forbids a zero-size
/// component); an untyped file's RecSize is either an explicit, valid
/// integer second argument, or -- when none is given -- TP's own documented
/// default of 128.  Either way codegen always has a concrete int64_t RecSize
/// by the time it reaches here, so plang_tp_reset/plang_tp_rewrite
/// themselves stay untouched (and are still called directly for a `text`
/// file, which has no RecSize concept of its own at all -- see
/// FileVarHelpers::isUntypedFileVar's own comment) -- these two wrap them.
///
/// A RecSize of 0 is real Borland/FPC field practice's own special case: it
/// sets InOutRes to 2 ("file not found") WITHOUT attempting to open
/// anything, exactly the way a genuinely missing file would.  F->RecSize is
/// left at whatever it already was (0 from a fresh file, or a still-good
/// prior value) rather than overwritten with the rejected 0, so a caller
/// that clears IOResult and retries with a sane RecSize is not left with a
/// stale 0 haunting a later BlockRead/BlockWrite that has not been wired up
/// yet (a later Cluster C item's job -- see PascalFileLayout.h's own
/// RecSize field comment).
void plang_tp_reset_sized(PascalFile *F, int64_t RecSize) {
    if (RecSize == 0) { setInOutResIfClear(2); return; }
    F->RecSize = RecSize;
    plang_tp_reset(F);
}

/// TP Rewrite's RecSize wiring -- see plang_tp_reset_sized just above for
/// the full rationale, identical here but for Rewrite.
void plang_tp_rewrite_sized(PascalFile *F, int64_t RecSize) {
    if (RecSize == 0) { setInOutResIfClear(2); return; }
    F->RecSize = RecSize;
    plang_tp_rewrite(F);
}

/// TP Close(f): closes the underlying stream with none of ISO plang_close's
/// three extra steps -- no closeFinalLine (see this section's top comment),
/// no std::free(F->Comp) (nothing on the TP open/close path above ever
/// allocates it; the dialect-agnostic plang_file_buffer is the only thing
/// that does, and it owns freeing it whenever it reallocates -- leaving a
/// stale allocation here after a program mixes typed-file component access
/// with TP-style Close is a bounded one-time leak, not a use-after-free),
/// and F->Name is left untouched -- real Turbo Pascal's Close does not
/// un-Assign a file (confirmed against `fpc -Mtp`: Reset/Rewrite/Append
/// with no intervening Assign after a Close reopen the same name), so a
/// following Reset/Rewrite/Append with no new Assign call correctly reopens
/// what was already bound.
void plang_tp_close(PascalFile *F) {
    closeStream(F);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    F->Mode     = PlangFmClosed;
    unloadComponent(F);
}

// ---- Cluster C item 6: FilePos/FileSize/Seek/Truncate/BlockRead/
// BlockWrite/Erase/Rename/Flush/SetTextBuf ----
//
// The rest of TP's own file model, on top of Assign/Reset/Rewrite/Append/
// Close just above.  Every one of these is Turbo-only and genuinely
// separate from any ISO/EP entry point (this section's own top comment,
// above plang_tp_assign, states the same P7 rule) -- none of them is
// reachable except through CodeGen's own Turbo-only dispatch.

/// TP FilePos(f): current record position, 0-relative, in units of
/// F->RecSize -- NOT bytes.  Confirmed against `fpc -Mtp`: FilePos reads 0
/// right after Reset on a fresh typed file, and 3 right after `Seek(f,
/// 3)`.  Non-aborting (tpFileReady): a closed or errored f reports 0
/// rather than crashing, consistent with every other Turbo entry point
/// here.
int64_t plang_tp_filepos(PascalFile *F) {
    if (!tpFileReady(F, "FilePos")) return 0;
    const long Pos = std::ftell(F->Fp);
    if (Pos < 0 || F->RecSize <= 0) return 0;
    return (int64_t)Pos / F->RecSize;
}

/// TP FileSize(f): total record count, in units of F->RecSize.  FLOORS
/// when the file's byte length is not an exact multiple of RecSize --
/// confirmed against `fpc -Mtp`: a 5-byte file reset with RecSize 2 reports
/// FileSize 2, not 3 (rounded up) and not 2.5 (there being no fractional
/// record). Saves and restores the current position around the SEEK_END
/// probe, the same way plang_lastposition/plang_empty above already do.
int64_t plang_tp_filesize(PascalFile *F) {
    if (!tpFileReady(F, "FileSize")) return 0;
    const long Saved = std::ftell(F->Fp);
    std::fseek(F->Fp, 0, SEEK_END);
    const long End = std::ftell(F->Fp);
    if (Saved >= 0) std::fseek(F->Fp, Saved, SEEK_SET);
    if (End < 0 || F->RecSize <= 0) return 0;
    return (int64_t)End / F->RecSize;
}

/// TP Seek(f, n): position f at record n (0-relative), n*F->RecSize bytes
/// from the start.  Seeking past the current end of file is legal and NOT
/// an error -- confirmed against `fpc -Mtp`: a following FilePos reads
/// back n exactly, with IOResult 0 -- real Turbo Pascal programs rely on
/// this to extend a file (seek past the end, then Write/BlockWrite).  A
/// negative n IS an error: confirmed `Seek(f, -5)` against `fpc -Mtp`
/// reports IOResult 218 (EINVAL, plang_tp_posix_to_run_error's own table),
/// which is what this reaches by capturing errno on fseek's own failure --
/// unlike SeekRead/SeekWrite/SeekUpdate's plang_err_seek_failed (an
/// unconditional abort), this goes through setInOutResIfClear, Turbo's own
/// {$I+}/{$I-} contract.  seekOffset's overflow-safe multiply (defined with
/// SeekRead/SeekWrite/SeekUpdate further down, forward-declared above) is
/// reused rather than duplicated, with IndexLow fixed at 0 -- Turbo's Seek
/// has no EP index-type origin to offset by.
void plang_tp_seek(PascalFile *F, int64_t N) {
    if (!tpFileReady(F, "Seek")) return;
    long Offset;
    const int64_t RecSize = F->RecSize > 0 ? F->RecSize : 1;
    if (!seekOffset(N, RecSize, 0, &Offset)
            || std::fseek(F->Fp, Offset, SEEK_SET) != 0) {
        const int Err = errno;
        setInOutResIfClear(plang_tp_posix_to_run_error(Err));
        return;
    }
    F->Buf = PlangFileUninit;
    unloadComponent(F);
}

/// TP Truncate(f): truncates f at the CURRENT position -- everything from
/// here to the previous end of file is discarded, and nothing before it is
/// touched.  ftruncate(2) on the underlying fd (fileno(F->Fp)), the same
/// low-level POSIX call plang_tp_reset's own directory guard already uses
/// fstat/fileno for -- no higher-level libc call for this exists.  Flushes
/// first: stdio may be holding buffered bytes not yet visible to the fd
/// truncate operates on, and truncating out from under an unflushed buffer
/// would let a later flush write stale data back past the new end.
void plang_tp_truncate(PascalFile *F) {
    if (!tpFileReady(F, "Truncate")) return;
    std::fflush(F->Fp);
    const long Pos = std::ftell(F->Fp);
    if (Pos < 0 || ftruncate(fileno(F->Fp), (off_t)Pos) != 0) {
        const int Err = errno;
        setInOutResIfClear(plang_tp_posix_to_run_error(Err));
        return;
    }
    unloadComponent(F);
}

/// TP BlockRead(f, buf, count, hasResult): reads count F->RecSize-sized
/// records from f into buf (an untyped, raw pointer -- CodeGen's own
/// EmitLValue on the actual argument, no type-specific marshalling),
/// returning the number of WHOLE records actually transferred (floor of
/// bytes-read / RecSize, matching FileSize's own floor above -- a
/// trailing partial record's bytes, if any, are read into buf but not
/// counted).  hasResult is the arity flag CodeGen's own dispatch passes
/// (CGProcCall.cpp): confirmed against `fpc -Mtp`, a short read WITHOUT a
/// result argument sets InOutRes 100 ("disk read error"); WITH one, it does
/// not -- the caller's own result variable (set by CodeGen after this
/// returns, not here) silently receives the actual count instead.  Reading
/// with Count <= 0 or F->RecSize <= 0 is a no-op that transfers zero
/// records and is never itself an error (Count <= 0 legitimately means "read
/// nothing"; RecSize <= 0 cannot happen after a real Reset -- Reset itself
/// already refuses a zero RecSize -- so this is defense in depth only).
int64_t plang_tp_blockread(PascalFile *F, void *Buf, int64_t Count, int8_t HasResult) {
    if (!tpFileReady(F, "BlockRead")) return 0;
    if (Count <= 0 || F->RecSize <= 0) return 0;
    const std::size_t Want = (std::size_t)Count * (std::size_t)F->RecSize;
    const std::size_t Got  = std::fread(Buf, 1, Want, F->Fp);
    const int64_t Actual = (int64_t)(Got / (std::size_t)F->RecSize);
    F->Buf = PlangFileUninit;
    unloadComponent(F);
    if (!HasResult && Actual < Count) setInOutResIfClear(100);
    return Actual;
}

/// TP BlockWrite(f, buf, count, hasResult): the write-side twin of
/// BlockRead just above -- see its own comment for the shared shape.  A
/// short write without a result argument sets InOutRes 101 ("disk write
/// error") instead of 100.
int64_t plang_tp_blockwrite(PascalFile *F, const void *Buf, int64_t Count, int8_t HasResult) {
    if (!tpFileReady(F, "BlockWrite")) return 0;
    if (Count <= 0 || F->RecSize <= 0) return 0;
    const std::size_t Want = (std::size_t)Count * (std::size_t)F->RecSize;
    const std::size_t Got  = std::fwrite(Buf, 1, Want, F->Fp);
    const int64_t Actual = (int64_t)(Got / (std::size_t)F->RecSize);
    unloadComponent(F);
    if (!HasResult && Actual < Count) setInOutResIfClear(101);
    return Actual;
}

/// TP Erase(f) / Rename(f, newname): act on F->Name (the name Assign bound
/// f to), requiring f be fmClosed first -- confirmed against `fpc -Mtp`:
/// calling either against a still-open f (or one never Assigned at all,
/// F->Mode's zero-init default, itself outside fmClosed..fmInOut) sets
/// InOutRes 102 ("file not assigned" -- FPC's own field practice reuses
/// that code here rather than a dedicated one) and performs nothing.
void plang_tp_erase(PascalFile *F) {
    if (F->Mode != PlangFmClosed) { setInOutResIfClear(102); return; }
    if (std::remove(F->Name) != 0) {
        const int Err = errno;
        setInOutResIfClear(plang_tp_posix_to_run_error(Err));
    }
}

/// TP Rename(f, newname): see plang_tp_erase's own comment for the shared
/// fmClosed requirement.  On success, updates F->Name to NewName -- real
/// Turbo Pascal's own documented behavior (confirmed against `fpc -Mtp`: a
/// Reset(f) with no intervening Assign, right after a successful Rename,
/// opens the NEW name) -- so a following Reset/Rewrite/Append on f reaches
/// the renamed file, not the one that no longer exists under the old name.
void plang_tp_rename(PascalFile *F, const char *NewName) {
    if (F->Mode != PlangFmClosed) { setInOutResIfClear(102); return; }
    if (std::rename(F->Name, NewName) != 0) {
        const int Err = errno;
        setInOutResIfClear(plang_tp_posix_to_run_error(Err));
        return;
    }
    const std::size_t Cap = static_cast<std::size_t>(PlangFileNameCap) - 1;
    const std::size_t Len = NewName ? std::strlen(NewName) : 0;
    const std::size_t N   = Len < Cap ? Len : Cap;
    if (N) std::memcpy(F->Name, NewName, N);
    F->Name[N] = '\0';
}

/// TP Flush(f): flushes f's buffered output without closing it.  No InOutRes
/// distinction of its own beyond tpFileReady's ordinary 103 -- confirmed
/// against `fpc -Mtp`: Flush on a valid, open file always reports IOResult
/// 0.
void plang_tp_flush(PascalFile *F) {
    if (!tpFileReady(F, "Flush")) return;
    std::fflush(F->Fp);
}

/// TP SetTextBuf(f, buf, size): overrides f's own internal I/O buffering
/// with caller-supplied storage, real Turbo Pascal's own documented
/// pre-open idiom.  plang's file model is a thin wrapper over C stdio, not
/// Borland's own hand-rolled TextRec buffering layer, and PascalFile (see
/// PascalFileLayout.h) carries no "pending buffer, not yet attached to a
/// stream" slot the way TextRec does -- so this is a DELIBERATE, DOCUMENTED
/// deviation from real Turbo Pascal's exact contract: called before f is
/// opened (F->Fp still null, the ordinary TP idiom -- confirmed working
/// against `fpc -Mtp`, this item's own manual test), there is no stream yet
/// for setvbuf(3) to attach to, so this is a silent no-op; called AFTER f
/// is opened, it takes effect immediately via setvbuf(3) (glibc allows
/// setvbuf at any point before the first real I/O, which an immediately
/// following Reset/Rewrite/Append never contradicts here since Reset/
/// Rewrite/Append always reopen the stream, discarding any buffer already
/// attached to it). A plang program wanting SetTextBuf to take effect must
/// therefore call it AFTER Reset/Rewrite/Append, the reverse of real Turbo
/// Pascal's own ordering -- a real, exercised limitation, not a guess.
void plang_tp_settextbuf(PascalFile *F, void *Buf, int64_t Size) {
    if (!F || !F->Fp || !Buf || Size <= 0) return;
    std::setvbuf(F->Fp, static_cast<char*>(Buf), _IOFBF, (std::size_t)Size);
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

// -std=turbo only: Eof(f)/Eoln(f)'s InOutRes-pending twins of
// plang_eof_file/plang_eoln_file just above -- real Turbo Pascal / Free
// Pascal field practice (System.Text's own Eof) reports TRUE the instant an
// I/O error is PENDING (InOutRes <> 0, not yet read-and-cleared through
// IOResult), rather than reflecting the file's actual position.  This is
// what lets `while not Eof(f) do ...` under `{$I-}` actually TERMINATE once
// a read starts failing, instead of retrying the same broken read forever
// -- the concrete behavior this item's own manual test (a truncated/
// malformed record) has to demonstrate.  Checked BEFORE tpFileReady: a file
// left open but now in an error state must not be treated as merely
// "closed" (InOutRes 103) just because this asks second -- InOutRes
// already holds whatever more specific code the failing operation itself
// set, and this must not stamp over it.  A closed/never-opened file (F->Fp
// null) reports true either way -- tpFileReady's own 103 assignment makes
// that fall out of the same InOutRes-pending check on the NEXT call, but
// the FIRST call still needs its own tpFileReady guard to answer true
// immediately rather than dereferencing a null F->Fp below.
int8_t plang_eof_file_turbo(PascalFile *F) {
    if (plang_tp_inoutres != 0) return 1;
    if (!tpFileReady(F, "eof")) return 1;
    // §6.6.5.2: rewrite(f) leaves eof(f) true, and it stays true for as long
    // as f is being generated -- see plang_eof_file's identical comment.
    if (!F->Readable) return 1;
    ensurePrimed(F);
    return (F->Buf == EOF) ? 1 : 0;
}

int8_t plang_eoln_file_turbo(PascalFile *F) {
    if (plang_tp_inoutres != 0) return 1;
    if (!tpFileReady(F, "eoln")) return 1;
    ensurePrimed(F);
    return (F->Buf == EOF || F->Buf == '\n') ? 1 : 0;
}

// -std=turbo only: SeekEof(f) / SeekEoln(f) -- Cluster C item 6.  Real
// Turbo Pascal / Free Pascal field practice (confirmed against `fpc -Mtp`):
// SeekEof skips past any blanks, tabs, carriage returns and line-feeds
// immediately ahead of the current position, actually CONSUMING them
// (unlike plain Eof, which only peeks through the one-character lookahead
// window), before testing eof -- deliberately NOT the same operation as
// plang_eof_file_turbo just above, which must not be aliased here: a
// following Eof/Eoln call has to see the position SeekEof actually left the
// stream at, not merely report what SeekEof itself computed. SeekEoln skips
// only blanks and tabs -- a line marker is what Eoln itself tests for, so
// SeekEoln stops there rather than crossing it (confirmed: a following
// Eoln right after SeekEoln is still true, and the newline itself is still
// there to be read next).  Neither is InOutRes-pending-aware the way Eof/
// Eoln's own `_turbo` siblings are -- Borland's own manual documents no
// such behavior for either, and there is no local `fpc -Mtp` field-practice
// evidence either way to match instead.
static void skipTurboWhitespace(PascalFile *F, bool CrossLines) {
    ensurePrimed(F);
    for (;;) {
        const int C = F->Buf;
        if (C == EOF) return;
        if (C == ' ' || C == '\t') { advance(F); continue; }
        if (CrossLines && (C == '\n' || C == '\r')) { advance(F); continue; }
        return;
    }
}

int8_t plang_tp_seekeof(PascalFile *F) {
    if (!tpFileReady(F, "SeekEof")) return 1;
    if (!F->Readable) return 1;
    skipTurboWhitespace(F, /*CrossLines=*/true);
    return (F->Buf == EOF) ? 1 : 0;
}

int8_t plang_tp_seekeoln(PascalFile *F) {
    if (!tpFileReady(F, "SeekEoln")) return 1;
    if (!F->Readable) return 1;
    skipTurboWhitespace(F, /*CrossLines=*/false);
    return (F->Buf == EOF || F->Buf == '\n') ? 1 : 0;
}

// ---- advance / flush ----

// ---- ISO §6.5.5: the buffer variable f^ ----

/// Issue #199: codegen loads and stores f^ at the component type's ABI
/// alignment whenever nothing tells IRBuilder otherwise -- the same default
/// that made a `packed record` field's store an empty promise (see
/// packedAccessAlign in lib/CodeGen/CGFieldAccess.cpp).  There the promise
/// could not be kept, because a packed field genuinely sits at an offset its
/// own type does not require, and the fix was to stop making it.  Here it CAN
/// be kept -- f^ is a fresh heap allocation, not a byte offset into something
/// else -- so this makes it true instead: `set of char` is `i256`, which this
/// project's data layout aligns to 16 (confirmed empirically fixing the
/// packed-field case: a `movaps` of an under-aligned i256 there SIGSEGVs from
/// -O1), and no wider scalar exists for a Pascal component to lower to. A
/// plain malloc does not documented-ly guarantee even that much -- glibc's
/// x86-64 allocator happens to hand back 16-aligned memory for any request
/// today, but nothing in the C or C++ standard requires it to, and other
/// allocators / targets are not obliged to follow suit.
inline constexpr std::size_t PlangFileBufferAlign = 16;

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
        // aligned_alloc requires the size to be a whole multiple of the
        // alignment; ElemSize need not be (a `file of char` asks for one
        // byte), so round up rather than passing ElemSize through directly.
        const std::size_t AllocSize =
            (static_cast<std::size_t>(ElemSize) + PlangFileBufferAlign - 1)
            / PlangFileBufferAlign * PlangFileBufferAlign;
        F->Comp = std::aligned_alloc(PlangFileBufferAlign, AllocSize);
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

// -std=turbo only: the fileReady twin of plang_readln_file just above --
// Readln is an ALL-dialect builtin (Builtins.def), so this is genuinely
// shared with ISO/EP and needs its own `_turbo` entry point.
void plang_readln_file_turbo(PascalFile *F) {
    if (!tpFileReady(F, "readln")) return;
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

// -std=turbo only: the fileReady twin of plang_writeln_file just above --
// called by BuiltinIO.cpp both for a bare `writeln(f)` and as the trailing
// newline after every `write(f, ...)`/`writeln(f, ...)` value, so this is
// one of the most heavily exercised `_turbo` siblings in this file.
void plang_writeln_file_turbo(PascalFile *F) {
    if (!tpFileReady(F, "writeln")) return;
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

/// ISO §6.9.1: reading a number skips preceding spaces and line terminators.
/// The text-file counterpart of plang_io.cpp's skipBlanks, against the
/// file's own lookahead window instead of plangInCh/plangInUnget -- text-file
/// reads and stdin/readstr reads are two independent implementations (issue
/// #237 is exactly that: they used to disagree), so each keeps its own copy
/// of this grammar rather than share one through an abstraction wide enough
/// to cover a FILE* and a memory buffer alike.
///
/// Primes unconditionally rather than through ensurePrimed, and checks
/// trapOnStreamError even when nothing turns out to be blank, not only
/// inside the loop: ensurePrimed leaves F->Buf at PlangFileUninit -- neither
/// EOF nor any real character -- for a file not currently readable, which is
/// exactly right for eof/eoln (see ensurePrimed's own comment: a program
/// that never reads must not block priming input it never asked for) but
/// wrong here, where the caller is already inside read(f, v) and genuinely
/// needs to know what is there. A file opened in the wrong direction fails
/// on its very first real character, and that failure has to be forced and
/// caught here, or scanNumberFile would read the untouched PlangFileUninit
/// sentinel as neither blank nor EOF and mistake the file for one that
/// starts with a malformed token instead of one open the wrong way.
static void skipBlanksFile(PascalFile *F) {
    if (F->Buf == PlangFileUninit) prime(F);
    trapOnStreamError(F, "read");
    while (F->Buf == ' ' || F->Buf == '\t' || F->Buf == '\n' || F->Buf == '\r') {
        advance(F);
        trapOnStreamError(F, "read");
    }
}

/// scanNumberFile's token buffer -- grown on demand rather than fixed
/// (issue #237), and file-scope so repeated reads reuse one allocation, the
/// same reason plang_io.cpp's TokBuf is.
static char*       NumTokBuf = nullptr;
static std::size_t NumTokCap = 0;

/// Collects the longest prefix of F that can form a number into NumTokBuf.
/// Digits only when \p Real is false; otherwise also a fractional part and
/// an exponent -- the same grammar plang_io.cpp's scanNumber recognizes,
/// against the file's own lookahead window instead of plangInCh/plangInUnget.
///
/// \p SawAny reports whether a non-blank character was available at all
/// before end-of-file, which is how the caller tells a malformed token
/// (issue #236: something was there and it didn't parse) apart from a
/// legitimate read past the end of the file (issue #284: nothing was there
/// to read).
static char *scanNumberFile(PascalFile *F, bool Real, bool &SawAny) {
    std::size_t N = 0;
    auto reserve = [&](std::size_t Need) {
        if (Need <= NumTokCap) return;
        std::size_t NewCap = NumTokCap ? NumTokCap : 64;
        while (NewCap < Need) NewCap *= 2;
        if (char *P = static_cast<char *>(std::realloc(NumTokBuf, NewCap))) {
            NumTokBuf = P;
            NumTokCap = NewCap;
        }
    };
    auto put = [&](int C) {
        reserve(N + 2);
        if (N + 1 < NumTokCap) NumTokBuf[N++] = static_cast<char>(C);
    };
    // Consumes the current lookahead character, checks the stream is still
    // healthy, and captures the character just consumed -- advance(F)
    // returns exactly what F->Buf held before the call.
    auto take = [&] {
        const int C = advance(F);
        trapOnStreamError(F, "read");
        put(C);
    };
    auto digits = [&] {
        while (F->Buf != EOF && std::isdigit(static_cast<unsigned char>(F->Buf)))
            take();
    };

    reserve(1);
    skipBlanksFile(F);
    SawAny = (F->Buf != EOF);
    if (F->Buf == '+' || F->Buf == '-') take();
    digits();
    if (Real) {
        if (F->Buf == '.') { take(); digits(); }
        if (F->Buf == 'e' || F->Buf == 'E') {
            advance(F);                    // consume 'e'/'E' itself...
            trapOnStreamError(F, "read");
            put('e');                      // ...store a normalized 'e'
            if (F->Buf == '+' || F->Buf == '-') take();
            digits();
        }
    }
    if (NumTokBuf) NumTokBuf[N] = '\0';
    return NumTokBuf;
}

/// Turbo's whole-token file reader -- the file-runtime twin of
/// plang_io.cpp's scanTokenTurbo, against the file's own lookahead window
/// instead of plangInCh/plangInUnget, for the same "text-file reads and
/// stdin/readstr reads are two independent implementations" reason
/// scanNumberFile's own comment gives.  Skips leading blanks, then collects
/// the WHOLE next whitespace-delimited token with no number-shaped
/// filtering, so plang_read_file_i64_turbo/plang_read_file_f64_turbo can
/// require the entire token to parse rather than just its longest
/// number-shaped prefix.
static char *scanTokenTurboFile(PascalFile *F, bool &SawAny) {
    std::size_t N = 0;
    auto reserve = [&](std::size_t Need) {
        if (Need <= NumTokCap) return;
        std::size_t NewCap = NumTokCap ? NumTokCap : 64;
        while (NewCap < Need) NewCap *= 2;
        if (char *P = static_cast<char *>(std::realloc(NumTokBuf, NewCap))) {
            NumTokBuf = P;
            NumTokCap = NewCap;
        }
    };
    auto put = [&](int C) {
        reserve(N + 2);
        if (N + 1 < NumTokCap) NumTokBuf[N++] = static_cast<char>(C);
    };

    reserve(1);
    skipBlanksFile(F);
    SawAny = (F->Buf != EOF);
    while (F->Buf != EOF && F->Buf != ' ' && F->Buf != '\t'
           && F->Buf != '\n' && F->Buf != '\r') {
        const int C = advance(F);
        trapOnStreamError(F, "read");
        put(C);
    }
    if (NumTokBuf) NumTokBuf[N] = '\0';
    return NumTokBuf;
}

/// The file-runtime twin of plang_io.cpp's turboRadixPrefix; see its own
/// comment for why a sign is deliberately not part of this.
static int turboRadixPrefixFile(const char *&Tok) {
    if (Tok[0] == '$') { ++Tok; return 16; }
    if (Tok[0] == '0' && (Tok[1] == 'x' || Tok[1] == 'X')) { Tok += 2; return 16; }
    if (Tok[0] == '&') { ++Tok; return 8; }
    if (Tok[0] == '%') { ++Tok; return 2; }
    return 10;
}

void plang_read_file_i64(PascalFile *F, int64_t *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanNumberFile(F, /*Real=*/false, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0; return; }                              // issue #284
    char *End = Tok;
    errno = 0;
    const long long V = Tok ? std::strtoll(Tok, &End, 10) : 0;
    if (!Tok || End == Tok) plang_err_read_format("read");         // issue #236
    if (errno == ERANGE) plang_err_read_int_range("read", Tok);    // issue #240
    *P = static_cast<int64_t>(V);
}

// The file-runtime twin of plang_io.cpp's plang_read_u64; see its own comment
// for why QWord (and only QWord) needs this instead of plang_read_file_i64.
void plang_read_file_u64(PascalFile *F, uint64_t *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanNumberFile(F, /*Real=*/false, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0; return; }                                // issue #284
    if (Tok && Tok[0] == '-') plang_err_read_format("read");
    char *End = Tok;
    errno = 0;
    const unsigned long long V = Tok ? std::strtoull(Tok, &End, 10) : 0;
    if (!Tok || End == Tok) plang_err_read_format("read");          // issue #236
    if (errno == ERANGE) plang_err_read_int_range("read", Tok);     // issue #240
    *P = static_cast<uint64_t>(V);
}

void plang_read_file_f64(PascalFile *F, double *P) {
    abortIfClosed(F, "read");
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanNumberFile(F, /*Real=*/true, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0.0; return; }                             // issue #284
    char *End = Tok;
    const double V = Tok ? std::strtod(Tok, &End) : 0.0;
    if (!Tok || End == Tok) plang_err_read_format("read");         // issue #236
    // A real that overflows to +/-HUGE_VAL is left alone -- see the matching
    // note in plang_io.cpp's plang_read_f64.
    *P = V;
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

// -std=turbo only: Read(f, ch: Char)'s fileReady twin of plang_read_file_char
// just above.  Char reads are the one BuiltinIO.cpp readFnSuffix case with
// no GRAMMAR difference from ISO/EP (see that function's own comment on why
// the numeric _i64/_f64/_u64 suffix swap skips char), so this exists purely
// for the fileReady/InOutRes choke point this item adds, not for a second
// parsing rule.
void plang_read_file_char_turbo(PascalFile *F, int8_t *P) {
    if (!tpFileReady(F, "read")) { *P = 0; return; }
    trapOnWrongDirection(F, "read", 0);
    ensurePrimed(F);
    if (F->Buf == EOF) { *P = 0; return; }
    const int C = advance(F);
    trapOnStreamError(F, "read");
    *P = static_cast<int8_t>(C == '\n' ? ' ' : C);
    unloadComponent(F);
}

// ---- Turbo read: whole-token, entire-token-must-parse, with $/0x/&/% radix
// prefixes -- the file-runtime twins of plang_io.cpp's plang_read_i64_turbo/
// plang_read_f64_turbo; see their own comments for the `fpc -Mtp` field
// practice this reproduces.

void plang_read_file_i64_turbo(PascalFile *F, int64_t *P) {
    if (!tpFileReady(F, "read")) { *P = 0; return; }
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanTokenTurboFile(F, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0; return; }                                // issue #284
    const char *Digits = Tok ? Tok : "";
    const int Radix = turboRadixPrefixFile(Digits);
    char *End = const_cast<char *>(Digits);
    errno = 0;
    const long long V = *Digits ? std::strtoll(Digits, &End, Radix) : 0;
    // See plang_io.cpp's plang_read_i64_turbo for why ERANGE gets the same
    // error as a malformed token, and why a value that fits int64_t but
    // overflows Turbo's own 16-bit Integer is not checked here at all.
    if (!*Digits || *End != '\0' || errno == ERANGE) plang_tp_runerror(106);
    *P = static_cast<int64_t>(V);
}

// The file-runtime twin of plang_io.cpp's plang_read_u64_turbo; see its own
// comment for the QWord-only reason and the leading-'-' rejection.
void plang_read_file_u64_turbo(PascalFile *F, uint64_t *P) {
    if (!tpFileReady(F, "read")) { *P = 0; return; }
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanTokenTurboFile(F, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0; return; }                                // issue #284
    const char *Digits = Tok ? Tok : "";
    if (*Digits == '-') plang_tp_runerror(106);
    const int Radix = turboRadixPrefixFile(Digits);
    char *End = const_cast<char *>(Digits);
    errno = 0;
    const unsigned long long V = *Digits ? std::strtoull(Digits, &End, Radix) : 0;
    if (!*Digits || *End != '\0' || errno == ERANGE) plang_tp_runerror(106);
    *P = static_cast<uint64_t>(V);
}

void plang_read_file_f64_turbo(PascalFile *F, double *P) {
    if (!tpFileReady(F, "read")) { *P = 0.0; return; }
    trapOnWrongDirection(F, "read", 0);
    bool SawAny = false;
    char *Tok = scanTokenTurboFile(F, SawAny);
    unloadComponent(F);
    if (!SawAny) { *P = 0.0; return; }                              // issue #284
    const char *Digits = Tok ? Tok : "";
    char *End = const_cast<char *>(Digits);
    const double V = *Digits ? std::strtod(Digits, &End) : 0.0;
    if (!*Digits || *End != '\0') plang_tp_runerror(106);
    // A real that overflows to +/-HUGE_VAL is left alone -- see the matching
    // note in plang_io.cpp's plang_read_f64_turbo.
    *P = V;
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

// -std=turbo only: the fileReady twin of plang_str_read_fixed_file just
// above -- reachable under Turbo via ISO §6.4.3.2's dialect-agnostic
// packed-array-of-char string-type (BuiltinIO.cpp's ExprIsCharStr), unlike
// plang_str_read_file's own VarString (EP's `string(n)`, whose syntax
// Parser::parseType gates to Opts.extendedPascal() only -- never
// constructible under Turbo, so that one reader gets no `_turbo` sibling at
// all).  On failure, pads Buf with spaces exactly as a zero-length read
// would -- matching the ISO version's own behavior for an already-blank
// line, not a new convention.
void plang_str_read_fixed_file_turbo(PascalFile *F, void *Buf, int64_t N) {
    auto* Data = static_cast<char*>(Buf);
    if (!tpFileReady(F, "read")) {
        for (int64_t I = 0; I < N; ++I) Data[I] = ' ';
        return;
    }
    int64_t Len = 0;
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

// -std=turbo only: the fileReady twin of plang_str_write_file just above --
// reachable under Turbo via ISO §6.4.3.2's dialect-agnostic
// packed-array-of-char string-type (BuiltinIO.cpp's ExprIsCharStr, marshalled
// through emitCharStrAsStr into the same {length, bytes} shape a VarString
// value already has, which is why this writer -- unlike plang_str_read_file
// on the read side -- is shared by both string shapes and so DOES need a
// `_turbo` sibling even though VarString itself never reaches Turbo code).
void plang_str_write_file_turbo(PascalFile *F, const void *S, int64_t /*Cap*/) {
    if (!tpFileReady(F, "write")) return;
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
    // W > 0 here: bound it before pacing the padding loop below one
    // character at a time, or an oversized W (write(f, s:maxint)) just
    // keeps calling fputc until it gets there (issue #247).
    checkedWidth(W);
    for (int64_t I = Len; I < W; ++I) std::fputc(' ', F->Fp);
    if (Len > W) Len = W;
    if (Len > 0)
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
    trapOnStreamError(F, "write");
}

// -std=turbo only: the fileReady twin of plang_str_write_file_w just above
// -- see plang_str_write_file_turbo's own comment for why the CharStr write
// path (unlike CharStr's own read) needs one.
void plang_str_write_file_w_turbo(PascalFile *F, const void *S, int64_t /*Cap*/,
                                   int64_t W) {
    if (!tpFileReady(F, "write")) return;
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
    checkedWidth(W);
    for (int64_t I = Len; I < W; ++I) std::fputc(' ', F->Fp);
    if (Len > W) Len = W;
    if (Len > 0)
        std::fwrite(Base + sizeof(int64_t), 1, static_cast<size_t>(Len), F->Fp);
    trapOnStreamError(F, "write");
}

// ---- Turbo string[N] (ShortString) file I/O ------------------------------
//
// plang_sstr.cpp's own comment gives the layout: a ONE-byte length prefix
// (byte 0), not string(N)'s eight, and the data starting right after it --
// so these are NOT plang_str_write_file/plang_str_read_file with a different
// header width spliced in, they read that one byte directly.  Mirrors
// plang_sstr.cpp's stdout/stdin shape (plang_sstr_write(_w)/plang_sstr_read)
// exactly, the same "file destination is a second family of writers, a
// generic plang_writeln_file(fp) adds the newline" convention every other
// typed write in this file already follows -- see BuiltinIO.cpp's
// emitWriteArgs ShortString branch for the CodeGen side this backs.

/// Writes the string[N] at S (a ShortString's one-byte length prefix,
/// followed by its data -- NOT null-terminated).
///
/// Uses tpFileReady, not abortIfClosed, DIRECTLY -- with no separate
/// `_turbo`-suffixed sibling and no codegen dispatch change needed at all,
/// unlike every other function this item converts.  This is deliberate, not
/// an inconsistency: ShortString (`string[N]`) is ALREADY Turbo-exclusive at
/// the parser level (Parser::parseType gates its `[` syntax on Opts.turbo(),
/// confirmed above plang_read_file_i64_turbo's own forward-declaration
/// block) -- no ISO 7185 or Extended Pascal program can ever construct a
/// ShortString-typed expression, so this function is already unreachable
/// from ISO/EP-compiled code, exactly the same way plang_tp_assign/
/// plang_tp_reset/... (this file's own "-std=turbo only" section, above)
/// are.  A `_turbo` sibling here would be a distinction with no call site to
/// justify it: this project's P7 rule protects a function two DIFFERENT
/// dialects can both reach; one only Turbo can ever reach needs no second,
/// identical copy of itself to protect it from.
void plang_sstr_write_file(PascalFile *F, const void *S, int64_t /*Cap*/) {
    if (!tpFileReady(F, "write")) return;
    const auto*   Base = static_cast<const char*>(S);
    const uint8_t Len  = static_cast<uint8_t>(Base[0]);
    if (Len > 0) {
        trapOnWrongDirection(F, "write", 1);
        std::fwrite(Base + 1, 1, static_cast<size_t>(Len), F->Fp);
        trapOnStreamError(F, "write");
    }
}

/// The same, in a field of W characters -- see plang_str_write_file_w's own
/// comment for the truncate/pad/negative-W rules this mirrors exactly.
void plang_sstr_write_file_w(PascalFile *F, const void *S, int64_t /*Cap*/,
                              int64_t W) {
    if (!tpFileReady(F, "write")) return;
    if (W == 0) return;
    trapOnWrongDirection(F, "write", 1);
    const auto*   Base = static_cast<const char*>(S);
    int64_t       Len  = static_cast<uint8_t>(Base[0]);
    if (W < 0) {
        if (Len > 0)
            std::fwrite(Base + 1, 1, static_cast<size_t>(Len), F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    checkedWidth(W);
    for (int64_t I = Len; I < W; ++I) std::fputc(' ', F->Fp);
    if (Len > W) Len = W;
    if (Len > 0)
        std::fwrite(Base + 1, 1, static_cast<size_t>(Len), F->Fp);
    trapOnStreamError(F, "write");
}

/// Fills the string[N] at S with the rest of the current line, clamped to
/// the (255-ceiling'd) capacity Cap -- the file-runtime twin of
/// plang_sstr_read (plang_sstr.cpp).  The terminator is left in the
/// lookahead, matching plang_str_read_file's own convention, so a following
/// readln consumes exactly one line.
void plang_sstr_read_file(PascalFile *F, void *S, int64_t Cap) {
    auto*   Base = static_cast<char*>(S);
    if (!tpFileReady(F, "read")) { Base[0] = 0; return; }
    char*   Data = Base + 1;
    int64_t Len  = 0;
    const int64_t ECap = Cap < 255 ? Cap : 255;
    trapOnWrongDirection(F, "read", 0);
    ensurePrimed(F);
    while (F->Buf != EOF && F->Buf != '\n') {
        const int C = advance(F);
        trapOnStreamError(F, "read");
        if (Len < ECap) Data[Len++] = static_cast<char>(C);
    }
    Base[0] = static_cast<char>(static_cast<uint8_t>(Len));
}

// ---- typed write (text file) ----

// Defined below with the rest of the field-width forms; the real writer with no
// width is the same one with the default.
void plang_write_file_f64_e(PascalFile *F, double V, int64_t W, int8_t Upper);
void plang_write_file_f64_f(PascalFile *F, double V, int64_t W, int64_t D, int8_t Upper);
void plang_write_file_f32_e(PascalFile *F, double V, int64_t W, int8_t Upper);
// -std=turbo only: the `_turbo` twins of the three just above -- see each
// one's own definition, alongside its ISO counterpart below, for why every
// scalar file writer in this section gets one (Write/Writeln are ALL-dialect
// builtins, so every one of these is genuinely shared with ISO/EP).
void plang_write_file_f64_e_turbo(PascalFile *F, double V, int64_t W, int8_t Upper);
void plang_write_file_f64_f_turbo(PascalFile *F, double V, int64_t W, int64_t D, int8_t Upper);
void plang_write_file_f32_e_turbo(PascalFile *F, double V, int64_t W, int8_t Upper);

// Upper: see plang_io.cpp's plang_write_bool for the convention -- CodeGen
// resolves Turbo's TRUE/FALSE vs ISO/EP's true/false from LangOptions.turbo()
// once, at the call site, and passes the answer in as this plain i8 flag.
void plang_write_file_i64 (PascalFile *F, int64_t     V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fprintf(F->Fp, "%" PRId64, V); trapOnStreamError(F, "write"); }
void plang_write_file_i64_turbo (PascalFile *F, int64_t V) { if (!tpFileReady(F,"write")) return; trapOnWrongDirection(F, "write", 1); std::fprintf(F->Fp, "%" PRId64, V); trapOnStreamError(F, "write"); }
// See plang_io.cpp's plang_write_u64 for why QWord (and only QWord) needs its
// own, unsigned-formatting entry point rather than reusing plang_write_file_i64.
void plang_write_file_u64 (PascalFile *F, uint64_t    V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fprintf(F->Fp, "%" PRIu64, V); trapOnStreamError(F, "write"); }
void plang_write_file_u64_turbo (PascalFile *F, uint64_t V) { if (!tpFileReady(F,"write")) return; trapOnWrongDirection(F, "write", 1); std::fprintf(F->Fp, "%" PRIu64, V); trapOnStreamError(F, "write"); }
void plang_write_file_f64 (PascalFile *F, double      V, int8_t Upper) { plang_write_file_f64_e(F, V, PlangRealWidth, Upper); }
void plang_write_file_f64_turbo (PascalFile *F, double V, int8_t Upper) { plang_write_file_f64_e_turbo(F, V, PlangRealWidth, Upper); }
// See plang_io.cpp's plang_write_f32 for why a promoted Single needs its own
// significant-digit-capped entry point rather than reusing plang_write_file_f64.
void plang_write_file_f32 (PascalFile *F, double      V, int8_t Upper) { plang_write_file_f32_e(F, V, PlangRealWidth, Upper); }
void plang_write_file_f32_turbo (PascalFile *F, double V, int8_t Upper) { plang_write_file_f32_e_turbo(F, V, PlangRealWidth, Upper); }
void plang_write_file_bool(PascalFile *F, int8_t      V, int8_t Upper) {
    abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1);
    std::fputs(Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"), F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_bool_turbo(PascalFile *F, int8_t V, int8_t Upper) {
    if (!tpFileReady(F,"write")) return; trapOnWrongDirection(F, "write", 1);
    std::fputs(Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"), F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_char(PascalFile *F, int8_t      V) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fputc(static_cast<unsigned char>(V), F->Fp); trapOnStreamError(F, "write"); }
void plang_write_file_char_turbo(PascalFile *F, int8_t V) { if (!tpFileReady(F,"write")) return; trapOnWrongDirection(F, "write", 1); std::fputc(static_cast<unsigned char>(V), F->Fp); trapOnStreamError(F, "write"); }
void plang_write_file_str (PascalFile *F, const char *S) { abortIfClosed(F,"write"); trapOnWrongDirection(F, "write", 1); std::fputs(S ? S : "", F->Fp); trapOnStreamError(F, "write"); }
void plang_write_file_str_turbo (PascalFile *F, const char *S) { if (!tpFileReady(F,"write")) return; trapOnWrongDirection(F, "write", 1); std::fputs(S ? S : "", F->Fp); trapOnStreamError(F, "write"); }

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
void plang_write_file_i64_w_turbo (PascalFile *F, int64_t V, int64_t W) {
    if (!tpFileReady(F,"write")) return;
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*" PRId64, checkedWidth(W), V);
    trapOnStreamError(F, "write");
}
// See plang_io.cpp's plang_write_u64_w for why QWord needs its own,
// unsigned-formatting field-width entry point.
void plang_write_file_u64_w (PascalFile *F, uint64_t V, int64_t W) {
    abortIfClosed(F,"write");
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*" PRIu64, checkedWidth(W), V);
    trapOnStreamError(F, "write");
}
void plang_write_file_u64_w_turbo (PascalFile *F, uint64_t V, int64_t W) {
    if (!tpFileReady(F,"write")) return;
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*" PRIu64, checkedWidth(W), V);
    trapOnStreamError(F, "write");
}
void plang_write_file_f64_e (PascalFile *F, double V, int64_t W, int8_t Upper) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO);
    std::fwrite(Buf, 1, N, F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_f64_e_turbo (PascalFile *F, double V, int64_t W, int8_t Upper) {
    if (!tpFileReady(F, "write")) return;
    trapOnWrongDirection(F, "write", 1);
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO);
    std::fwrite(Buf, 1, N, F->Fp);
    trapOnStreamError(F, "write");
}
// See plang_io.cpp's plang_write_f32_e for why a promoted Single needs its
// own significant-digit-capped field-width entry point.
void plang_write_file_f32_e (PascalFile *F, double V, int64_t W, int8_t Upper) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO,
                                           PlangSingleMaxDecPlaces);
    std::fwrite(Buf, 1, N, F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_f32_e_turbo (PascalFile *F, double V, int64_t W, int8_t Upper) {
    if (!tpFileReady(F, "write")) return;
    trapOnWrongDirection(F, "write", 1);
    char Buf[PlangRealMaxChars];
    const std::size_t N = plangFormatReal(Buf, V, W, Upper ? PlangRealProfileTurbo : PlangRealProfileISO,
                                           PlangSingleMaxDecPlaces);
    std::fwrite(Buf, 1, N, F->Fp);
    trapOnStreamError(F, "write");
}
// A negative FracDigits falls back to the same exponential format omitting
// the decimals clause entirely produces, exactly as plang_write_file_cplx_w's
// own per-component formatting already did before it started calling this.
void plang_write_file_f64_f (PascalFile *F, double V, int64_t W, int64_t D, int8_t Upper) {
    abortIfClosed(F,"write");
    if (D < 0) { plang_write_file_f64_e(F, V, W, Upper); return; }
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*.*f", checkedWidth(W), checkedWidth(D), V);
    trapOnStreamError(F, "write");
}
void plang_write_file_f64_f_turbo (PascalFile *F, double V, int64_t W, int64_t D, int8_t Upper) {
    if (!tpFileReady(F,"write")) return;
    if (D < 0) { plang_write_file_f64_e_turbo(F, V, W, Upper); return; }
    trapOnWrongDirection(F, "write", 1);
    std::fprintf(F->Fp, "%*.*f", checkedWidth(W), checkedWidth(D), V);
    trapOnStreamError(F, "write");
}
// §6.9.3.6: the field is exactly W characters, so a longer string loses its
// tail; the `%*s` a width otherwise maps onto pads but never truncates.
// §6.9.3.5 writes a boolean as its char-string would be written, truncation
// included.  A negative W is written in full, as if no width had been given.
// Every rule here is ISO/EP's; Turbo reverses the truncating part -- \p
// NoTrunc (see plang_io.cpp's plangOutPadded for the full rationale, checked
// against `fpc -Mtp`) makes W a MINIMUM instead: the value is always written
// in full, and W only ever adds padding, even when W is 0.
static void writePadded(PascalFile *F, const char *S, int64_t W, int8_t NoTrunc) {
    if (!NoTrunc && W == 0) return;
    trapOnWrongDirection(F, "write", 1);
    const std::size_t Len = S ? std::strlen(S) : 0;
    if (W < 0) {
        if (Len) std::fwrite(S, 1, Len, F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    // W > 0 here (or W == 0 under NoTrunc, where checkedWidth(0) == 0 and the
    // pad loop below simply does not run): same reasoning as plang_io.cpp's
    // plangOutPadded -- this paces its own padding loop rather than handing W
    // to printf's `%*d`/`%*c`, so without checkedWidth's INT32_MAX trap an
    // oversized W just pads one character at a time until it gets there
    // (issue #247).
    const auto Width = static_cast<std::size_t>(checkedWidth(W));
    for (std::size_t I = Len; I < Width; ++I) std::fputc(' ', F->Fp);
    if (NoTrunc) {
        if (Len) std::fwrite(S, 1, Len, F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    if (Len) std::fwrite(S, 1, Len < Width ? Len : Width, F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_bool_w(PascalFile *F, int8_t V, int64_t W, int8_t Upper, int8_t NoTrunc) {
    abortIfClosed(F,"write");
    writePadded(F, Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"), W, NoTrunc);
}
// writePadded (just above) calls neither abortIfClosed nor tpFileReady
// itself -- every caller, ISO and Turbo alike, checks first and calls in
// only once the file is known ready -- so the `_turbo` siblings in this
// field-width group reuse it directly rather than duplicating its body.
void plang_write_file_bool_w_turbo(PascalFile *F, int8_t V, int64_t W, int8_t Upper, int8_t NoTrunc) {
    if (!tpFileReady(F,"write")) return;
    writePadded(F, Upper ? (V ? "TRUE" : "FALSE") : (V ? "true" : "false"), W, NoTrunc);
}
// AlwaysWrite: see plang_io.cpp's plang_write_char_w for the convention
// (checked against `fpc -Mtp`) -- Turbo's zero-width char write still
// writes the character, where ISO/EP's writes nothing.
void plang_write_file_char_w(PascalFile *F, int8_t V, int64_t W, int8_t AlwaysWrite) {
    abortIfClosed(F,"write");
    if (W == 0 && !AlwaysWrite) return;
    trapOnWrongDirection(F, "write", 1);
    if (W < 0) {
        std::fputc(static_cast<unsigned char>(V), F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    std::fprintf(F->Fp, "%*c", checkedWidth(W), static_cast<unsigned char>(V));
    trapOnStreamError(F, "write");
}
void plang_write_file_char_w_turbo(PascalFile *F, int8_t V, int64_t W, int8_t AlwaysWrite) {
    if (!tpFileReady(F,"write")) return;
    if (W == 0 && !AlwaysWrite) return;
    trapOnWrongDirection(F, "write", 1);
    if (W < 0) {
        std::fputc(static_cast<unsigned char>(V), F->Fp);
        trapOnStreamError(F, "write");
        return;
    }
    std::fprintf(F->Fp, "%*c", checkedWidth(W), static_cast<unsigned char>(V));
    trapOnStreamError(F, "write");
}
void plang_write_file_str_w (PascalFile *F, const char *S, int64_t W, int8_t NoTrunc)
    { abortIfClosed(F,"write"); writePadded(F, S, W, NoTrunc); }
void plang_write_file_str_w_turbo (PascalFile *F, const char *S, int64_t W, int8_t NoTrunc)
    { if (!tpFileReady(F,"write")) return; writePadded(F, S, W, NoTrunc); }

// EP §6.9.3.6: a complex is written as a parenthesized pair of reals — in the
// representation reals are written in, which is why each half goes through the
// real writer rather than being formatted alongside the parentheses.
void plang_write_file_cplx (PascalFile *F, double Re, double Im, int8_t Upper) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    std::fputc('(', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64(F, Re, Upper);
    std::fputc(',', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64(F, Im, Upper);
    std::fputc(')', F->Fp);
    trapOnStreamError(F, "write");
}
void plang_write_file_cplx_w(PascalFile *F, double Re, double Im,
                             int64_t W, int64_t D, int8_t Upper) {
    abortIfClosed(F,"write");
    trapOnWrongDirection(F, "write", 1);
    // plang_write_file_f64_f already picks between "%*.*f" and the
    // exponential fallback on D's sign, which used to be duplicated here.
    std::fputc('(', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64_f(F, Re, W, D, Upper);
    std::fputc(',', F->Fp);
    trapOnStreamError(F, "write");
    plang_write_file_f64_f(F, Im, W, D, Upper);
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

// -std=turbo only: the fileReady twin of plang_read_binary just above --
// `file of T` typed binary files carry no dialect gate of their own
// (Sema::resolveType's FileTypeNode arm), so Read(f, v) on one is reachable
// from Turbo exactly as it is from ISO/EP, and needs the same choke point
// every text-file operation in this section gets.
//
// Tier 3 Cluster C item 5 audit: prime()/unloadComponent() here are NOT both
// the same thing.  prime() is NOT ISO's f^ buffer-variable bookkeeping in
// disguise -- it also drives plang_eof_file_turbo (this file, "eof reads the
// window" comment on plang_read_binary above applies here too: eof(f) under
// -std=turbo peeks one byte/char ahead via F->Buf exactly the way ISO/EP's
// does), so it stays.  unloadComponent(), on the other hand, only resets
// F->CompLoaded, which nothing under Turbo ever reads again: F->Comp/
// CompLoaded exist solely to back f^ (plang_file_buffer, FileVarHelpers.cpp
// fileBufferPtr), and `f^` is already rejected under Turbo at Sema (#477,
// TypeContext/SemaExpr's buffer-variable-access check).  Calling it here was
// genuinely dead work -- not a bug (F->CompLoaded has no other reader), just
// wasted -- so it is dropped from the two _turbo functions; plang_read_binary/
// plang_write_binary (the ISO/EP twins, just above/below) keep it, since EP's
// own f^ is very much reachable there.
void plang_read_binary_turbo(PascalFile *F, void *Buf, int64_t ElemSize) {
    if (ElemSize > 0) std::memset(Buf, 0, static_cast<std::size_t>(ElemSize));
    if (!tpFileReady(F, "read")) return;
    trapOnWrongDirection(F, "read", 0);
    std::fread(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "read");
    prime(F);
}

void plang_write_binary(PascalFile *F, const void *Buf, int64_t ElemSize) {
    abortIfClosed(F, "write");
    trapOnWrongDirection(F, "write", 1);
    std::fwrite(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "write");
    unloadComponent(F);
}

// -std=turbo only: the fileReady twin of plang_write_binary just above --
// see plang_read_binary_turbo's own comment for why typed binary files need
// one at all, and for why unloadComponent() (which plang_write_binary above
// still calls) is dropped here.
void plang_write_binary_turbo(PascalFile *F, const void *Buf, int64_t ElemSize) {
    if (!tpFileReady(F, "write")) return;
    trapOnWrongDirection(F, "write", 1);
    std::fwrite(Buf, static_cast<std::size_t>(ElemSize), 1, F->Fp);
    trapOnStreamError(F, "write");
}

// ---- EP §6.7.5.2: extend / update ----

void plang_extend(PascalFile *F, const char *Name, int8_t IsText) {
    // §6.4.3.5: extending reuses or reopens F's stream out from under
    // whatever it was writing before -- finish that line first (issue #234),
    // same as plang_reset/plang_rewrite/plang_close.  This is also what
    // keeps a subsequent writeln from gluing onto an unterminated line left
    // by an earlier rewrite+write+extend with no close in between.
    if (IsText) closeFinalLine(F);
    // Issue #411: see the identical note in plang_reset/plang_rewrite
    // (issue #239) -- Name is about to be overwritten by the bind()/
    // retained-name fallback just below, so whether it was the caller's own
    // explicit argument has to be captured first.
    const bool HasExplicitName = Name && Name[0] != '\0';
    // EP §6.7.5.6 / issue #411: a file bound to an external entity (via
    // bind(), or via #239's retained-name convention for a name given
    // directly to an earlier reset/rewrite/extend/update) reopens that same
    // entity even when extend is called without an explicit name -- unlike
    // plang_reset/plang_rewrite, extend and update never called
    // findBinding() at all, so a name-less extend used to fall straight
    // through to the internal-tmpfile path below regardless of any name the
    // file had previously been opened with, and whatever it wrote vanished
    // the moment that tmpfile was closed instead of being appended to the
    // real, on-disk file.
    if (!HasExplicitName) Name = findBinding(F);
    if (!Name || Name[0] == '\0') {
        // Internal file: seek to end of existing temp storage.
        if (F->Fp) {
            std::fflush(F->Fp);
            std::fseek(F->Fp, 0, SEEK_END);
        } else {
            F->Fp      = openTemp("extend");
            F->Binding = PlangBindTemp;
        }
        clearWritePath(F);
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
        setWritePath(F, Name);
        // Issue #239/#411: retain an explicit name the same way bind() does,
        // so a later reset/rewrite/extend/update with no name reopens this
        // same external entity instead of silently diverting to fresh,
        // unnamed internal storage.
        if (HasExplicitName) setBinding(F, Name);
    }
    F->Buf      = PlangFileUninit;
    // 2, not 1: extend opens both directions (named files get "r+b" above,
    // same as update), and trapOnWrongDirection treats 2 as the one value
    // that is exempt from its internal-file direction check.
    F->Readable = 2;
    unloadComponent(F);
}

void plang_update(PascalFile *F, const char *Name, int8_t IsText) {
    // §6.4.3.5 / issue #234: see the same call in plang_extend just above.
    if (IsText) closeFinalLine(F);
    // Issue #411: see the identical note in plang_extend just above (and in
    // plang_reset/plang_rewrite, issue #239).
    const bool HasExplicitName = Name && Name[0] != '\0';
    // EP §6.7.5.6 / issue #411: see the identical note in plang_extend just
    // above -- update never called findBinding() either.
    if (!HasExplicitName) Name = findBinding(F);
    if (!Name || Name[0] == '\0') {
        // Internal file: reposition to start without truncating.
        if (F->Fp) {
            std::fflush(F->Fp);
            std::rewind(F->Fp);
        } else {
            F->Fp      = openTemp("update");
            F->Binding = PlangBindTemp;
        }
        clearWritePath(F);
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
        setWritePath(F, Name);
        // Issue #239/#411: retain an explicit name the same way bind() does,
        // so a later reset/rewrite/extend/update with no name reopens this
        // same external entity instead of silently diverting to fresh,
        // unnamed internal storage.
        if (HasExplicitName) setBinding(F, Name);
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
//
// The subtraction and multiply that compute that offset are themselves
// plain, unchecked int64_t arithmetic, so a huge caller-supplied n can
// overflow the multiply and wrap around to a small, in-range-looking offset
// -- e.g. a file[1..100] of integer (ElemSize 8) with n = 2^61: (n - 1) is
// 2^61, and 2^61 * 8 is exactly 2^64, which wraps to 0, indistinguishable
// from a legitimate seek to the very first record. fseek would then happily
// honor that wrapped offset and return success, so its own failure check
// above never fires -- this is the same silent-corruption failure mode
// #233 closed, just reached through overflow instead of through a value
// fseek itself rejects (issue #403). Checked with __builtin_{sub,mul}_
// overflow before the offset ever reaches fseek, the same idiom already
// used for int64 overflow elsewhere in the runtime (plang_math.cpp's
// plang_ipow/plang_sqr_int); either operation overflowing, or fseek itself
// then failing on whatever offset resulted, reports through the same
// plang_err_seek_failed as #233's fix -- a value that overflows the very
// arithmetic that produces a byte offset is exactly as "not reachable in
// this file" as one fseek itself refuses.

static bool seekOffset(int64_t N, int64_t ElemSize, int64_t IndexLow,
                        long *Offset) {
    int64_t Diff, Off;
    if (__builtin_sub_overflow(N, IndexLow, &Diff)) return false;
    if (__builtin_mul_overflow(Diff, ElemSize, &Off)) return false;
    *Offset = static_cast<long>(Off);
    return true;
}

void plang_seekread(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekRead");
    long Offset;
    if (!seekOffset(N, ElemSize, IndexLow, &Offset) ||
        std::fseek(F->Fp, Offset, SEEK_SET) != 0)
        plang_err_seek_failed("SeekRead", N);
    F->Readable = 1;
    unloadComponent(F);
    prime(F);
}

void plang_seekwrite(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekWrite");
    long Offset;
    if (!seekOffset(N, ElemSize, IndexLow, &Offset) ||
        std::fseek(F->Fp, Offset, SEEK_SET) != 0)
        plang_err_seek_failed("SeekWrite", N);
    F->Buf      = PlangFileUninit;
    F->Readable = 0;
    unloadComponent(F);
}

void plang_seekupdate(PascalFile *F, int64_t N, int64_t ElemSize, int64_t IndexLow) {
    abortIfClosed(F, "SeekUpdate");
    long Offset;
    if (!seekOffset(N, ElemSize, IndexLow, &Offset) ||
        std::fseek(F->Fp, Offset, SEEK_SET) != 0)
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
// Pascal programs rarely open more than a handful of files.  PLANG_MAX_BINDINGS
// and PLANG_MAX_NAME_LEN are defined with WritePathTable, above, which needs
// the same two constants for the same reason.

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
    // back what it bound.  EP §6.7.5.6: the binding itself survives a close,
    // too -- it is removed only by an explicit unbind (or replaced by another
    // bind), so `bound` must answer from the binding table rather than from
    // whether the file happens to be open right now.  Gating on F->Fp used to
    // report a just-bound-but-not-yet-opened file as unbound, and a bound
    // file as unbound again the moment it was closed (issue #248).
    if (const char* Name = findBinding(F)) {
        auto N = static_cast<int64_t>(std::strlen(Name));
        if (N > PlangMaxBindingName) N = PlangMaxBindingName;
        std::memcpy(Out->name.data, Name, static_cast<size_t>(N));
        Out->name.len = N;
        Out->bound = 1;
    }
    // Standard streams (input/output program parameters) are always bound.
    if (F->Fp && F->Binding == PlangBindStd) Out->bound = 1;
}

} // extern "C"

} // namespace plang
