#pragma once

/// PascalFileLayout.h — the one declaration of the file record
///
/// A Pascal file variable is a PascalFile, and two very different things have
/// to agree about what that is: the runtime, which reads and writes its fields
/// as an ordinary C++ struct, and codegen, which lays out storage for every
/// file variable a program declares and hands the runtime a pointer to it.
///
/// The two used to say it separately — the struct here, and the equivalent
/// LLVM StructType in fileStructType() — with nothing but a `sizeof` assert on
/// the runtime side to hold them together.  That assert could not see the
/// codegen encoding at all, so a field added, widened or reordered on one side
/// alone left generated code reading a field at an offset nothing had written
/// it to, with no diagnostic anywhere: the sizes still matched.
///
/// So the struct is declared once, here, and codegen checks the type it builds
/// against this one field by field (see fileStructType()).  Neither side can
/// now be changed alone.  This follows plang/Basic/Arith.h, which the runtime
/// and the constant folders share for the same reason — one definition, so
/// that two answers cannot disagree.
///
/// The layout is for LP64.  Codegen hard-codes `ptr`, `i64` and `i32`, so the
/// two agree on any target where those are 8, 8 and 4 bytes; the check in
/// fileStructType() is what says so out loud on one where they are not.

#include <cstdint>
#include <cstdio>

namespace plang {

/// The one-character lookahead window is not yet primed.  See plang_file.cpp.
inline constexpr int PlangFileUninit = -2;

/// What Fp is attached to.  Determines what a rewrite/reset with no file name
/// means: rebinding a standard stream would be wrong, but an internal file
/// must get fresh temporary storage rather than the terminal.
enum PlangBinding : int8_t {
    PlangBindNone = 0, ///< closed, or opened from an explicit file name
    PlangBindTemp = 1, ///< tmpfile(): an ISO "internal" file
    PlangBindStd  = 2, ///< stdin/stdout via a program file-parameter
};

/// Capacity of PascalFile.Name (below) -- TP's Assign(f, name) bound
/// filename.  512 matches this project's existing precedent for a bound
/// filename's storage: plang_file.cpp's own WritePathTable/BindingTable
/// (PLANG_MAX_NAME_LEN) use the identical capacity for the same kind of
/// value, just kept in a side table instead of on the struct itself.  A
/// fixed embedded buffer rather than a pointer: Assign has to work before
/// the file has ever been opened or anything allocated for it, and a fixed
/// buffer needs no ownership/free discipline in Close (see plang_tp_close).
inline constexpr int PlangFileNameCap = 512;

/// TP's fmClosed/fmInput/fmOutput/fmInOut -- the real Borland-documented
/// magic values, EMPIRICALLY CONFIRMED rather than assumed: a local
/// `fpc -Mtp` build reading back `TextRec(f).Mode` after Rewrite/Close/
/// Reset/Append reported exactly 0xD7B2/0xD7B0/0xD7B1/0xD7B2 (Append shares
/// fmOutput with Rewrite).  Stored in PascalFile.Mode below.  0 -- Mode's
/// zero-initialized default for a file variable Assign has never touched --
/// is outside this range on both ends (0 < 0xD7B0), which is what lets a
/// later item's Reset/Rewrite range check (fmClosed..fmInOut) treat an
/// unassigned file as an error "for free" rather than needing its own
/// distinct sentinel.
inline constexpr int32_t PlangFmClosed = 0xD7B0;
inline constexpr int32_t PlangFmInput  = 0xD7B1;
inline constexpr int32_t PlangFmOutput = 0xD7B2;
inline constexpr int32_t PlangFmInOut  = 0xD7B3;

struct PascalFile {
    std::FILE *Fp       = nullptr;
    /// ISO §6.5.5: storage for the buffer variable f^, allocated on first use
    /// because only the generated code knows how wide a component is.
    void      *Comp     = nullptr;
    int64_t    CompSize = 0;
    int        Buf      = PlangFileUninit;
    int8_t     Binding  = PlangBindNone;
    /// The file's current direction: 0 = write-only (rewrite, seekwrite),
    /// 1 = read-only (reset, seekread), 2 = both (extend/update/seekupdate).
    /// Existing boolean reads of this field (peeking, eof) only ask whether
    /// it is nonzero, which 2 still satisfies; plang_file.cpp's
    /// trapOnWrongDirection is what tells 1 and 2 apart, for an internal
    /// (tmpfile()-backed) file where the C stream itself cannot enforce a
    /// direction (see issue #152).
    int8_t     Readable = 0;
    /// Whether Comp holds the component at the current position.  Cleared
    /// wherever the position moves, so the next access reads it afresh.
    int8_t     CompLoaded = 0;

    // ---- -std=turbo only below.  ISO/EP code never reads or writes these
    // three, but they live on the ONE struct every dialect's file variable
    // shares (see this header's own top comment) -- appended at the end, so
    // no field above moves and no ISO/EP field offset changes.

    /// TP Assign(f, name): the bound filename, or an empty string to mean
    /// "bind to the console" -- a following Reset binds to stdin, a
    /// following Rewrite/Append to stdout (a deliberate TP idiom, confirmed
    /// against `fpc -Mtp`).  See PlangFileNameCap's own comment for why this
    /// is a fixed buffer and not a pointer.
    char       Name[PlangFileNameCap] = {};
    /// TP's fmClosed/fmInput/fmOutput/fmInOut -- see those constants' own
    /// comment just above for the confirmed values and the zero-init claim.
    int32_t    Mode = 0;
    /// TP Reset/Rewrite's second (RecSize) argument for an untyped file --
    /// NOT wired up by anything yet (a sibling item does that); this field
    /// only exists so that item does not also have to grow this shared
    /// struct.  Always 0 today.
    int64_t    RecSize = 0;
};

/// Every field, in declaration order, as (member, the LLVM type codegen builds
/// it from).  Both the offset checks in fileStructType() and any future reader
/// walk this list, so a field added to the struct without a line here is a
/// field codegen never learns about — and the count assert below says so.
#define PLANG_FILE_FIELDS(X)                                            \
    X(Fp,         ptrTy)                                               \
    X(Comp,       ptrTy)                                               \
    X(CompSize,   i64Ty)                                               \
    X(Buf,        i32Ty)                                               \
    X(Binding,    i8Ty)                                                \
    X(Readable,   i8Ty)                                                \
    X(CompLoaded, i8Ty)                                                \
    X(Name,       llvm::ArrayType::get(i8Ty, PlangFileNameCap))        \
    X(Mode,       i32Ty)                                               \
    X(RecSize,    i64Ty)

/// How many fields PLANG_FILE_FIELDS lists.  fileStructType() asserts the type
/// it builds has exactly this many elements, so a field left out of the list
/// is caught rather than silently dropped from the layout.
inline constexpr unsigned PlangFileFieldCount = 10;

} // namespace plang
