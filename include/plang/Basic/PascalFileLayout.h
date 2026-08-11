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

struct PascalFile {
    std::FILE *Fp       = nullptr;
    /// ISO §6.5.5: storage for the buffer variable f^, allocated on first use
    /// because only the generated code knows how wide a component is.
    void      *Comp     = nullptr;
    int64_t    CompSize = 0;
    int        Buf      = PlangFileUninit;
    int8_t     Binding  = PlangBindNone;
    /// Whether the stream may be read, so that filling f^ by peeking is only
    /// attempted where peeking can work.
    int8_t     Readable = 0;
    /// Whether Comp holds the component at the current position.  Cleared
    /// wherever the position moves, so the next access reads it afresh.
    int8_t     CompLoaded = 0;
};

/// Every field, in declaration order, as (member, the LLVM type codegen builds
/// it from).  Both the offset checks in fileStructType() and any future reader
/// walk this list, so a field added to the struct without a line here is a
/// field codegen never learns about — and the count assert below says so.
#define PLANG_FILE_FIELDS(X) \
    X(Fp,         ptrTy)     \
    X(Comp,       ptrTy)     \
    X(CompSize,   i64Ty)     \
    X(Buf,        i32Ty)     \
    X(Binding,    i8Ty)      \
    X(Readable,   i8Ty)      \
    X(CompLoaded, i8Ty)

/// How many fields PLANG_FILE_FIELDS lists.  fileStructType() asserts the type
/// it builds has exactly this many elements, so a field left out of the list
/// is caught rather than silently dropped from the layout.
inline constexpr unsigned PlangFileFieldCount = 7;

} // namespace plang
