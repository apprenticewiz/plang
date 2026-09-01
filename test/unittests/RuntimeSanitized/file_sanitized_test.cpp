/// file_sanitized_test.cpp — issue #190 part B option 1
///
/// Calls runtime/plang_file.cpp's public C-ABI file-variable entry points
/// directly, linked from plang_runtime_sanitized (an ASan+UBSan-instrumented
/// build of the SAME runtime/*.cpp sources plang_runtime itself compiles --
/// see runtime/CMakeLists.txt's PLANG_ENABLE_RUNTIME_SANITIZER_TESTS block).
/// No driver, no compiled Pascal program, no lit: a PascalFile is built
/// directly from plang/Basic/PascalFileLayout.h -- the one shared C++ struct
/// codegen's own fileStructType() is checked against -- and fed straight to
/// plang_reset/plang_close/etc. the way CodeGen-emitted IR would, but with
/// deliberately adversarial arguments guardheap (a black-box allocator
/// wrapper over compiled programs) can never construct: a double-close
/// (a use-after-free-shaped call sequence plang_close is specifically
/// documented to survive), an already-closed file handed to a real I/O call
/// (the "use-after-close" shape plang_file.cpp's own abortIfClosed traps),
/// a nil PascalFile*/PlangBindingType* fed to the three EP bind entry points
/// (each documented nil-safe), and boundary-sized (exact heap allocation,
/// zero-length buffer) reads/writes through the VarString file I/O path.
///
/// Every PascalFile below is heap-allocated (std::make_unique), not a stack
/// local: plang_bind/plang_reset/plang_rewrite key an internal, process-wide
/// BindingTable/WritePathTable off the PascalFile*'s own address (see
/// plang_file.cpp's findBinding/findWritePath), and this project's own
/// allocator does not quickly reuse a freed heap address the way a
/// repeatedly-entered stack frame could -- so two unrelated TEST()s here
/// cannot alias one another's leftover table entry the way two stack-local
/// PascalFile variables at the same frame offset conceivably could.  Any
/// test that binds one explicitly unbinds it before returning, for the same
/// reason.

#include "plang/Basic/PascalFileLayout.h"
#include "plang/Basic/RequiredRecordLayouts.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using plang::PascalFile;
using plang::PlangBindingType;
using plang::PlangMaxBindingName;

// ---- Declarations of the entry points this file calls -- extern "C" strips
// away plang_file.cpp's own `namespace plang { extern "C" { ... } }`
// wrapping entirely (extern "C" linkage carries no namespace mangling), so
// these need only match the real parameter types, not the original
// declaration's namespace nesting. ------------------------------------------
extern "C" {
void    plang_reset(PascalFile *F, const char *Name, int8_t IsText);
void    plang_rewrite(PascalFile *F, const char *Name, int8_t IsText);
void    plang_close(PascalFile *F, int8_t IsText);
int8_t  plang_eof_file(PascalFile *F);
int8_t  plang_eoln_file(PascalFile *F);
void   *plang_file_buffer(PascalFile *F, int64_t ElemSize, int8_t IsText);
void    plang_get_file(PascalFile *F, int64_t ElemSize);
void    plang_put_file(PascalFile *F, int64_t ElemSize);
void    plang_str_read_file(PascalFile *F, void *S, int64_t Cap);
void    plang_str_write_file(PascalFile *F, const void *S, int64_t Cap);
void    plang_bind(PascalFile *F, const PlangBindingType *B);
void    plang_unbind(PascalFile *F);
void    plang_binding(PascalFile *F, PlangBindingType *Out);
}

namespace {

/// Reads back S's embedded int64 length prefix -- the same {length, bytes}
/// VarString layout plang_str_read_file/plang_str_write_file both use (see
/// their own comments in runtime/plang_file.cpp).
int64_t varStringLen(const void *S) {
    int64_t Len;
    std::memcpy(&Len, S, sizeof(Len));
    return Len;
}

} // namespace

// ---- plang_reset / plang_rewrite / plang_close on an internal file --------

TEST(RuntimeSanitizedFile, ResetWithNilNameOnAFreshFileOpensAnEmptyInternalFile) {
    // A freshly heap-allocated, default-member-initialized PascalFile is
    // exactly the state CodeGen leaves an as-yet-untouched file variable in
    // (PascalFileLayout.h's own default member initializers) -- Fp/Comp
    // null, Buf uninitialized, nothing bound.
    auto F = std::make_unique<PascalFile>();
    plang_reset(F.get(), nullptr, /*IsText=*/1);
    EXPECT_TRUE(plang_eof_file(F.get()));
    EXPECT_TRUE(plang_eoln_file(F.get()));
    plang_close(F.get(), /*IsText=*/1);
}

TEST(RuntimeSanitizedFile, RewriteWithEmptyStringNameThenResetRereadsTheSameInternalFile) {
    // "" is documented identically to nullptr (HasExplicitName checks
    // Name[0] != '\0' too) -- a real generated Rewrite(f) with no name
    // argument reaches this exact call shape.
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), "", /*IsText=*/1);
    ASSERT_NE(F->Fp, nullptr);
    // Writes directly through the real FILE* plang_rewrite just opened --
    // PascalFile.Fp is a public field, exactly what a real writeln(f, ...)
    // call eventually reaches through plang_str_write_file/friends.
    std::fputs("hello\n", F->Fp);
    // No explicit name: plang_reset's own "F->Fp already open" branch
    // fflush()es and rewind()s this SAME temporary file rather than opening
    // a second one -- confirmed below by reading "hello" back out of it.
    plang_reset(F.get(), nullptr, /*IsText=*/1);
    EXPECT_FALSE(plang_eof_file(F.get()));

    std::vector<char> Buf(sizeof(int64_t) + 5); // exact boundary: "hello" is 5 chars
    plang_str_read_file(F.get(), Buf.data(), 5);
    EXPECT_EQ(varStringLen(Buf.data()), 5);
    EXPECT_EQ(std::memcmp(Buf.data() + sizeof(int64_t), "hello", 5), 0);

    plang_close(F.get(), /*IsText=*/1);
}

TEST(RuntimeSanitizedFile, ClosingAnAlreadyClosedFileIsSafe) {
    // A double-close-shaped call sequence: plang_close is specifically
    // documented (see its own comment) to leave F in a state a second close
    // finds nothing left to free (Comp already null, Fp already null via
    // closeStream) -- exercised here under ASan specifically to confirm the
    // second std::free(F->Comp) is never reached with a stale, already-freed
    // pointer.
    auto F = std::make_unique<PascalFile>();
    plang_reset(F.get(), nullptr, /*IsText=*/1);
    plang_file_buffer(F.get(), /*ElemSize=*/4, /*IsText=*/0); // allocates F->Comp
    plang_close(F.get(), /*IsText=*/1);
    plang_close(F.get(), /*IsText=*/1); // must not double-free F->Comp or crash
}

// ---- plang_file_buffer / get / put at boundary element sizes --------------

TEST(RuntimeSanitizedFile, FileBufferClampsAnElemSizeOfZeroToOneByte) {
    // ElemSize < 1 is documented to clamp to 1 rather than pass 0 through to
    // aligned_alloc (which POSIX leaves implementation-defined for a
    // zero-byte request) -- an argument no real `file of T` codegen would
    // ever pass (every Pascal type has a positive size) but the ABI itself
    // does not forbid.
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), nullptr, /*IsText=*/0);
    void *Comp = plang_file_buffer(F.get(), /*ElemSize=*/0, /*IsText=*/0);
    ASSERT_NE(Comp, nullptr);
    // The clamped 1-byte component is genuinely writable/readable -- not
    // just a non-null pointer -- confirming the clamp, not just survival.
    *static_cast<uint8_t *>(Comp) = 0xAB;
    EXPECT_EQ(*static_cast<uint8_t *>(Comp), 0xAB);
    plang_close(F.get(), /*IsText=*/0);
}

TEST(RuntimeSanitizedFile, GetPutFileRoundTripsAnExactlySizedComponent) {
    // ElemSize=64: large enough that a fixed small internal scratch buffer
    // would overflow first, and plang_file_buffer's own aligned_alloc
    // rounds up to a 64-byte (already-16-aligned) request with zero slack.
    constexpr int64_t ElemSize = 64;
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), nullptr, /*IsText=*/0);

    void *Comp = plang_file_buffer(F.get(), ElemSize, /*IsText=*/0);
    ASSERT_NE(Comp, nullptr);
    std::memset(Comp, 0x5A, static_cast<std::size_t>(ElemSize));
    plang_put_file(F.get(), ElemSize); // appends f^ to the file

    // Turn the file around to read the one component back.
    plang_reset(F.get(), nullptr, /*IsText=*/0);
    void *ReadComp = plang_file_buffer(F.get(), ElemSize, /*IsText=*/0);
    ASSERT_NE(ReadComp, nullptr);
    std::vector<uint8_t> Expected(static_cast<std::size_t>(ElemSize), 0x5A);
    EXPECT_EQ(std::memcmp(ReadComp, Expected.data(), Expected.size()), 0);

    plang_get_file(F.get(), ElemSize); // advance past the one component
    EXPECT_TRUE(plang_eof_file(F.get()));

    plang_close(F.get(), /*IsText=*/0);
}

// ---- plang_str_read_file / plang_str_write_file at Cap boundaries ---------

TEST(RuntimeSanitizedFile, StrReadFileWithZeroCapacityTouchesOnlyTheLengthPrefix) {
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), "", /*IsText=*/1);
    std::fputs("hello\n", F->Fp);
    plang_reset(F.get(), nullptr, /*IsText=*/1);

    // Cap=0: the boundary-sized buffer this item's own checklist asks for --
    // exactly the 8-byte length prefix, no data area at all. Len must come
    // back 0 (truncating-assignment semantics), and nothing may be written
    // past byte 8.
    std::vector<char> Buf(sizeof(int64_t));
    plang_str_read_file(F.get(), Buf.data(), /*Cap=*/0);
    EXPECT_EQ(varStringLen(Buf.data()), 0);

    plang_close(F.get(), /*IsText=*/1);
}

TEST(RuntimeSanitizedFile, StrReadFileTruncatesExactlyAtCapWithNoOverflow) {
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), "", /*IsText=*/1);
    std::fputs("hello world\n", F->Fp); // 11 chars, longer than Cap below
    plang_reset(F.get(), nullptr, /*IsText=*/1);

    constexpr int64_t Cap = 4;
    std::vector<char> Buf(sizeof(int64_t) + Cap); // exact boundary, no slack
    plang_str_read_file(F.get(), Buf.data(), Cap);
    EXPECT_EQ(varStringLen(Buf.data()), Cap);
    EXPECT_EQ(std::memcmp(Buf.data() + sizeof(int64_t), "hell", Cap), 0);

    plang_close(F.get(), /*IsText=*/1);
}

TEST(RuntimeSanitizedFile, StrWriteFileWithZeroLengthWritesNothing) {
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), "", /*IsText=*/1);

    // Base is an 8-byte length-prefix-only buffer with Len=0 and no data
    // area at all -- the write path's own `if (Len > 0)` guard must never
    // read the byte immediately past this allocation.
    std::vector<char> Base(sizeof(int64_t), 0);
    plang_str_write_file(F.get(), Base.data(), /*Cap=*/0);

    plang_close(F.get(), /*IsText=*/1);
}

TEST(RuntimeSanitizedFile, StrWriteFileReadsExactlyLenBytesWithNoOverread) {
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), "", /*IsText=*/1);

    constexpr int64_t Len = 5;
    std::vector<char> Base(sizeof(int64_t) + Len); // exact boundary, no slack
    std::memcpy(Base.data(), &Len, sizeof(Len));
    std::memcpy(Base.data() + sizeof(int64_t), "abcde", Len);
    plang_str_write_file(F.get(), Base.data(), /*Cap=*/0);

    // Read it back to confirm exactly Len bytes were written (not more, not
    // fewer), not just that the write survived ASan's redzone.
    plang_reset(F.get(), nullptr, /*IsText=*/1);
    std::vector<char> ReadBuf(sizeof(int64_t) + Len);
    plang_str_read_file(F.get(), ReadBuf.data(), Len);
    EXPECT_EQ(varStringLen(ReadBuf.data()), Len);
    EXPECT_EQ(std::memcmp(ReadBuf.data() + sizeof(int64_t), "abcde", Len), 0);

    plang_close(F.get(), /*IsText=*/1);
}

// ---- plang_bind / plang_unbind / plang_binding with nil arguments ---------

TEST(RuntimeSanitizedFile, BindUnbindBindingAllToleranceNilArgumentsTheAbiDoesNotForbid) {
    // No real `uses` clause can construct these calls (bind/unbind/binding
    // always take the file variable EP §6.7.5.6 syntax requires), but the
    // C-ABI entry point itself takes a raw PascalFile*/PlangBindingType*
    // with no compile-time way to forbid nil -- each is documented
    // (`if (!F) return;` / `if (!Out) return;`) to no-op rather than crash.
    plang_bind(nullptr, nullptr);
    plang_unbind(nullptr);
    plang_binding(nullptr, nullptr);

    auto F = std::make_unique<PascalFile>();
    plang_binding(F.get(), nullptr); // Out nil, F non-nil: still a no-op

    PlangBindingType Out{};
    Out.bound = 1;
    Out.name.len = 99; // deliberately garbage, to confirm binding() overwrites it
    plang_binding(nullptr, &Out); // F nil, Out non-nil: zeroes Out and returns
    EXPECT_EQ(Out.bound, 0);
    EXPECT_EQ(Out.name.len, 0);

    plang_bind(F.get(), nullptr); // valid F, nil binding: clears any binding, no crash
}

TEST(RuntimeSanitizedFile, BindThenBindingRoundTripsAMaximumLengthName) {
    // PlangMaxBindingName (255) is BindingType.name's own real boundary --
    // not an arbitrary large number -- heap-allocated (not stack) so this
    // test's own PascalFile address cannot alias a stale BindingTable entry
    // left by an unrelated TEST() (see this file's own top comment).
    auto F = std::make_unique<PascalFile>();
    auto B = std::make_unique<PlangBindingType>();
    B->bound = 0;
    B->name.len = PlangMaxBindingName;
    std::memset(B->name.data, 'N', PlangMaxBindingName);

    plang_bind(F.get(), B.get());

    auto Out = std::make_unique<PlangBindingType>();
    plang_binding(F.get(), Out.get());
    EXPECT_EQ(Out->bound, 1);
    EXPECT_EQ(Out->name.len, PlangMaxBindingName);
    EXPECT_EQ(std::memcmp(Out->name.data, B->name.data, PlangMaxBindingName), 0);

    plang_unbind(F.get()); // leave no stale BindingTable entry behind
}

// ---- Death tests: the runtime's own deliberate traps, confirmed to run
// clean under ASan/UBSan rather than being masked by a sanitizer crash of
// their own. --------------------------------------------------------------

TEST(RuntimeSanitizedFileDeathTest, OperatingOnAClosedFileTrapsCleanlyInsteadOfCorruptingMemory) {
    // A "use-after-close" call sequence -- the file-handle-error-path analog
    // of use-after-free this item's own checklist asks for. abortIfClosed
    // (runtime/plang_file.cpp) is the trap: every ISO/EP entry point calls it
    // first and terminates the process immediately on a null F->Fp, rather
    // than dereferencing it -- via exit(70) (issue #301) as of this test,
    // not the std::abort() this used to be; EXPECT_DEATH below is agnostic
    // to which of the two actually ends the process. Confirmed here that
    // the trap itself is clean under ASan -- no heap-corruption report masks
    // the intended, documented trap.
    auto F = std::make_unique<PascalFile>();
    plang_rewrite(F.get(), nullptr, /*IsText=*/0);
    plang_close(F.get(), /*IsText=*/0);
    EXPECT_DEATH(plang_get_file(F.get(), /*ElemSize=*/1), "file not open in 'get'");
}

TEST(RuntimeSanitizedFileDeathTest, BindingAnAlreadyBoundFileTrapsCleanlyInsteadOfSilentlyReplacingIt) {
    // EP §6.7.5.6: rebinding an already-bound file variable is a dynamic-
    // violation, reported through plang_err_bind_already_bound
    // (std::exit(70), runtime/plang_sys.cpp's own PlangRuntimeErrorStatus) --
    // confirmed clean under ASan the same way as the death test above.
    auto F = std::make_unique<PascalFile>();
    auto B = std::make_unique<PlangBindingType>();
    B->bound = 0;
    B->name.len = 4;
    std::memcpy(B->name.data, "test", 4);

    EXPECT_EXIT(
        {
            plang_bind(F.get(), B.get());
            plang_bind(F.get(), B.get()); // second bind of the same F: traps
        },
        ::testing::ExitedWithCode(70), "bind of a file that is already bound");
}
