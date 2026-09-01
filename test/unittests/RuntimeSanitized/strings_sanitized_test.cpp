/// strings_sanitized_test.cpp — issue #190 part B option 1
///
/// Calls the real pas_strings$* entry points behind runtime/plang_strings.cpp
/// (the Strings unit's implementation) directly, linked from
/// plang_runtime_sanitized (an ASan+UBSan-instrumented build of the SAME
/// runtime/*.cpp sources plang_runtime itself compiles -- see runtime/
/// CMakeLists.txt's PLANG_ENABLE_RUNTIME_SANITIZER_TESTS block).  No driver,
/// no compiled Pascal program: this is the runtime's public C-ABI surface
/// exercised in-process, with adversarial arguments guardheap (a black-box
/// allocator wrapper over compiled programs) can never reach -- a nil
/// pointer where a real `uses Strings` caller would never pass one but the
/// ABI itself does not forbid it, a zero-length buffer, a boundary-sized
/// (exactly-fits) heap allocation.
///
/// Every buffer below is heap-allocated to EXACTLY the size the call needs,
/// never padded "to be safe": a std::vector/std::unique_ptr sized precisely
/// to the boundary is what gives ASan's own redzone something to catch --
/// a one-byte-too-large buffer would silently absorb an off-by-one write
/// that a correctly-sized one turns into an immediate heap-buffer-overflow
/// report.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// ---- Declarations matching runtime/plang_strings.cpp's own asm-label scheme
// exactly (that file's own header comment explains why: CGLinkage's mangled
// "pas_strings$Name" link name is not something an ordinary C++ declaration
// can spell on its own, so both sides bind through the identical
// asm("...")/asm("_...") label rather than a shared header neither the
// runtime nor codegen otherwise needs). --------------------------------------
#if defined(__APPLE__)
#define PLANG_ASM_NAME(name) asm("_" name)
#else
#define PLANG_ASM_NAME(name) asm(name)
#endif

extern "C" {
uint32_t plang_strings_StrLen(const char *Str) PLANG_ASM_NAME("pas_strings$StrLen");
char    *plang_strings_StrCopy(char *Dest, const char *Source) PLANG_ASM_NAME("pas_strings$StrCopy");
char    *plang_strings_StrLCopy(char *Dest, const char *Source, uint32_t MaxLen)
    PLANG_ASM_NAME("pas_strings$StrLCopy");
char    *plang_strings_StrCat(char *Dest, const char *Source) PLANG_ASM_NAME("pas_strings$StrCat");
char    *plang_strings_StrLCat(char *Dest, const char *Source, uint32_t MaxLen)
    PLANG_ASM_NAME("pas_strings$StrLCat");
int16_t  plang_strings_StrComp(const char *Str1, const char *Str2) PLANG_ASM_NAME("pas_strings$StrComp");
int16_t  plang_strings_StrLComp(const char *Str1, const char *Str2, uint32_t MaxLen)
    PLANG_ASM_NAME("pas_strings$StrLComp");
int16_t  plang_strings_StrIComp(const char *Str1, const char *Str2) PLANG_ASM_NAME("pas_strings$StrIComp");
char    *plang_strings_StrPos(const char *Str1, const char *Str2) PLANG_ASM_NAME("pas_strings$StrPos");
char    *plang_strings_StrScan(const char *Str, char Chr) PLANG_ASM_NAME("pas_strings$StrScan");
char    *plang_strings_StrRScan(const char *Str, char Chr) PLANG_ASM_NAME("pas_strings$StrRScan");
char    *plang_strings_StrUpper(char *Str) PLANG_ASM_NAME("pas_strings$StrUpper");
char    *plang_strings_StrLower(char *Str) PLANG_ASM_NAME("pas_strings$StrLower");
char    *plang_strings_StrNew(const char *Str) PLANG_ASM_NAME("pas_strings$StrNew");
void     plang_strings_StrDispose(char *Str) PLANG_ASM_NAME("pas_strings$StrDispose");
char    *plang_strings_StrPCopy(char *Dest, const void *Source) PLANG_ASM_NAME("pas_strings$StrPCopy");
char    *plang_strings_StrPLCopy(char *Dest, const void *Source, uint32_t MaxLen)
    PLANG_ASM_NAME("pas_strings$StrPLCopy");
} // extern "C"

namespace {

// Heap-allocates a PChar buffer of exactly Len+1 bytes (Len characters plus
// the terminator) and copies Text (which must be exactly Len characters
// long) into it, terminator included. The returned buffer has no slack at
// either end, so ASan's redzone sits immediately after byte Len.
std::unique_ptr<char[]> exactPChar(const char *Text) {
    const std::size_t Len = std::strlen(Text);
    auto Buf = std::make_unique<char[]>(Len + 1);
    std::memcpy(Buf.get(), Text, Len + 1);
    return Buf;
}

} // namespace

// ---- StrLen -----------------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrLenOfExactlyOneByteEmptyString) {
    // The smallest possible PChar: a single-byte allocation holding only the
    // terminator -- no slack for an off-by-one read to hide in.
    auto Buf = std::make_unique<char[]>(1);
    Buf[0] = '\0';
    EXPECT_EQ(plang_strings_StrLen(Buf.get()), 0u);
}

// ---- StrCopy / StrLCopy ------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrCopyIntoExactlySizedDest) {
    auto Source = exactPChar("hello");
    auto Dest   = std::make_unique<char[]>(6); // 5 chars + terminator, no slack
    EXPECT_EQ(plang_strings_StrCopy(Dest.get(), Source.get()), Dest.get());
    EXPECT_STREQ(Dest.get(), "hello");
}

TEST(RuntimeSanitizedStrings, StrLCopyWithZeroMaxLenTouchesOnlyTheTerminator) {
    auto Source = exactPChar("hello");
    auto Dest   = std::make_unique<char[]>(1); // room for the terminator only
    plang_strings_StrLCopy(Dest.get(), Source.get(), 0);
    EXPECT_EQ(Dest[0], '\0');
}

TEST(RuntimeSanitizedStrings, StrLCopyTruncatesExactlyAtDestBoundary) {
    auto Source = exactPChar("hello world"); // 11 chars, longer than MaxLen
    auto Dest   = std::make_unique<char[]>(4); // MaxLen=3 + terminator, no slack
    plang_strings_StrLCopy(Dest.get(), Source.get(), 3);
    EXPECT_STREQ(Dest.get(), "hel");
}

TEST(RuntimeSanitizedStrings, StrLCopySourceShorterThanMaxLenStopsAtSourceEnd) {
    auto Source = exactPChar("hi");
    auto Dest   = std::make_unique<char[]>(3); // 2 chars + terminator
    plang_strings_StrLCopy(Dest.get(), Source.get(), 100); // MaxLen far exceeds both buffers
    EXPECT_STREQ(Dest.get(), "hi");
}

// ---- StrCat / StrLCat ---------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrCatIntoExactlySizedDest) {
    // Dest already holds "foo"; appending "bar" needs exactly 7 bytes total.
    auto Dest = std::make_unique<char[]>(7);
    std::memcpy(Dest.get(), "foo", 4);
    auto Source = exactPChar("bar");
    plang_strings_StrCat(Dest.get(), Source.get());
    EXPECT_STREQ(Dest.get(), "foobar");
}

TEST(RuntimeSanitizedStrings, StrLCatNoOpWhenDestAlreadyAtMaxLen) {
    // DestLen (3) >= MaxLen (3): documented no-op, must not read Source or
    // write past Dest's own exact 4-byte allocation.
    auto Dest = std::make_unique<char[]>(4);
    std::memcpy(Dest.get(), "abc", 4);
    auto Source = exactPChar("xyz");
    plang_strings_StrLCat(Dest.get(), Source.get(), 3);
    EXPECT_STREQ(Dest.get(), "abc");
}

TEST(RuntimeSanitizedStrings, StrLCatTruncatesExactlyAtDestBoundary) {
    // "ab" (2) + as much of "hello" as fits in a total of 4 -> "ab" + "he".
    auto Dest = std::make_unique<char[]>(5); // MaxLen(4) + terminator, no slack
    std::memcpy(Dest.get(), "ab", 3);
    auto Source = exactPChar("hello");
    plang_strings_StrLCat(Dest.get(), Source.get(), 4);
    EXPECT_STREQ(Dest.get(), "abhe");
}

// ---- StrComp / StrLComp / StrIComp -------------------------------------------

TEST(RuntimeSanitizedStrings, StrCompOfTwoEmptyOneByteStrings) {
    auto A = std::make_unique<char[]>(1); A[0] = '\0';
    auto B = std::make_unique<char[]>(1); B[0] = '\0';
    EXPECT_EQ(plang_strings_StrComp(A.get(), B.get()), 0);
}

TEST(RuntimeSanitizedStrings, StrLCompWithZeroMaxLenNeverDereferencesEitherArg) {
    // MaxLen=0: strncmp(3)'s own contract says zero bytes are compared, so
    // this must not read past either one-byte (terminator-only) buffer.
    auto A = std::make_unique<char[]>(1); A[0] = 'x';
    auto B = std::make_unique<char[]>(1); B[0] = 'y';
    EXPECT_EQ(plang_strings_StrLComp(A.get(), B.get(), 0), 0);
}

TEST(RuntimeSanitizedStrings, StrICompCaseInsensitiveOnExactlySizedBuffers) {
    auto A = exactPChar("HELLO");
    auto B = exactPChar("hello");
    EXPECT_EQ(plang_strings_StrIComp(A.get(), B.get()), 0);
}

// ---- StrPos -------------------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrPosWithEmptyNeedleMatchesImmediately) {
    auto Haystack = exactPChar("hello");
    auto Needle   = std::make_unique<char[]>(1); Needle[0] = '\0';
    EXPECT_EQ(plang_strings_StrPos(Haystack.get(), Needle.get()), Haystack.get());
}

TEST(RuntimeSanitizedStrings, StrPosNotFoundReturnsNil) {
    auto Haystack = exactPChar("hello");
    auto Needle   = exactPChar("z");
    EXPECT_EQ(plang_strings_StrPos(Haystack.get(), Needle.get()), nullptr);
}

// ---- StrScan / StrRScan ---------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrScanForTheTerminatorItselfOnAnExactlySizedBuffer) {
    // Chr=#0 is documented to find the terminator itself -- exercised on a
    // buffer with zero slack past it, so a one-past-the-end read would trip
    // ASan rather than land in unrelated allocator bookkeeping.
    auto Buf = exactPChar("abc");
    char *Found = plang_strings_StrScan(Buf.get(), '\0');
    EXPECT_EQ(Found, Buf.get() + 3);
}

TEST(RuntimeSanitizedStrings, StrRScanOnAOneByteEmptyStringFindsTheTerminator) {
    auto Buf = std::make_unique<char[]>(1); Buf[0] = '\0';
    EXPECT_EQ(plang_strings_StrRScan(Buf.get(), '\0'), Buf.get());
}

// ---- StrUpper / StrLower --------------------------------------------------------

TEST(RuntimeSanitizedStrings, StrUpperInPlaceOnExactlySizedBuffer) {
    auto Buf = exactPChar("MiXeD");
    EXPECT_EQ(plang_strings_StrUpper(Buf.get()), Buf.get());
    EXPECT_STREQ(Buf.get(), "MIXED");
}

TEST(RuntimeSanitizedStrings, StrLowerOnAOneByteEmptyStringIsANoOp) {
    auto Buf = std::make_unique<char[]>(1); Buf[0] = '\0';
    plang_strings_StrLower(Buf.get());
    EXPECT_EQ(Buf[0], '\0');
}

// ---- StrNew / StrDispose ----------------------------------------------------
//
// These two are the one pair here that themselves allocate/free through this
// project's own GetMem/FreeMem (plang_tp_getmem/plang_tp_freemem,
// runtime/plang_sys.cpp) -- exactly the sequence a real `Dispose(StrNew(...))`
// pair reaches, exercised directly with no driver or compiled program.

TEST(RuntimeSanitizedStrings, StrNewOfNilReturnsNilAndStrDisposeOfNilIsANoOp) {
    // Documented Borland/FPC field practice: StrNew(nil) = nil,
    // StrDispose(nil) is a no-op -- a nil argument neither entry point's ABI
    // forbids, and a real `uses Strings` caller could reach either
    // (StrNew(nil) legitimately, StrDispose(nil) via a variable that was
    // never assigned).
    EXPECT_EQ(plang_strings_StrNew(nullptr), nullptr);
    plang_strings_StrDispose(nullptr); // must not crash
}

TEST(RuntimeSanitizedStrings, StrNewOfEmptyStringAllocatesExactlyOneByte) {
    auto Source = std::make_unique<char[]>(1); Source[0] = '\0';
    char *Copy = plang_strings_StrNew(Source.get());
    ASSERT_NE(Copy, nullptr);
    EXPECT_STREQ(Copy, "");
    plang_strings_StrDispose(Copy);
}

TEST(RuntimeSanitizedStrings, StrNewThenStrDisposeRoundTripsABoundarySizedString) {
    // A boundary-sized (4096-character) heap allocation, per this item's own
    // "boundary-sized allocations" ask: large enough that a fixed-size
    // internal scratch buffer elsewhere in the runtime would overflow first.
    const std::size_t Len = 4096;
    auto Source = std::make_unique<char[]>(Len + 1);
    std::memset(Source.get(), 'A', Len);
    Source[Len] = '\0';

    char *Copy = plang_strings_StrNew(Source.get());
    ASSERT_NE(Copy, nullptr);
    EXPECT_EQ(plang_strings_StrLen(Copy), Len);
    EXPECT_EQ(std::memcmp(Copy, Source.get(), Len + 1), 0);
    plang_strings_StrDispose(Copy); // exact-size FreeMem matching StrNew's GetMem
}

// ---- StrPCopy / StrPLCopy (ShortString -> PChar) -----------------------------
//
// Source is a raw pointer to this project's own ShortString layout (byte 0 =
// length, bytes 1.. = data, NOT null-terminated) -- see plang_strings.cpp's
// own header comment. Both the ShortString buffer and the PChar destination
// below are heap-allocated to the exact boundary size the call needs.

TEST(RuntimeSanitizedStrings, StrPCopyOfAnEmptyShortString) {
    auto ShortStr = std::make_unique<uint8_t[]>(1);
    ShortStr[0] = 0; // length byte: zero-length ShortString, no data bytes at all
    auto Dest = std::make_unique<char[]>(1); // just the terminator
    plang_strings_StrPCopy(Dest.get(), ShortStr.get());
    EXPECT_STREQ(Dest.get(), "");
}

TEST(RuntimeSanitizedStrings, StrPCopyOfAMaximumLengthShortString) {
    // ShortString's length byte is a single uint8_t, so 255 is its own real
    // maximum -- not an arbitrary large number, the actual type boundary.
    const uint8_t Len = 255;
    auto ShortStr = std::make_unique<uint8_t[]>(1 + Len);
    ShortStr[0] = Len;
    std::memset(ShortStr.get() + 1, 'Q', Len);
    auto Dest = std::make_unique<char[]>(static_cast<std::size_t>(Len) + 1);
    plang_strings_StrPCopy(Dest.get(), ShortStr.get());
    EXPECT_EQ(plang_strings_StrLen(Dest.get()), Len);
    EXPECT_EQ(Dest[Len], '\0');
}

TEST(RuntimeSanitizedStrings, StrPLCopyTruncatesExactlyAtDestBoundary) {
    const uint8_t Len = 10;
    auto ShortStr = std::make_unique<uint8_t[]>(1 + Len);
    ShortStr[0] = Len;
    std::memcpy(ShortStr.get() + 1, "0123456789", Len);
    auto Dest = std::make_unique<char[]>(5); // MaxLen=4 + terminator, no slack
    plang_strings_StrPLCopy(Dest.get(), ShortStr.get(), 4);
    EXPECT_STREQ(Dest.get(), "0123");
}

TEST(RuntimeSanitizedStrings, StrPLCopyWithZeroMaxLenTouchesOnlyTheTerminator) {
    const uint8_t Len = 3;
    auto ShortStr = std::make_unique<uint8_t[]>(1 + Len);
    ShortStr[0] = Len;
    std::memcpy(ShortStr.get() + 1, "abc", Len);
    auto Dest = std::make_unique<char[]>(1);
    plang_strings_StrPLCopy(Dest.get(), ShortStr.get(), 0);
    EXPECT_EQ(Dest[0], '\0');
}
