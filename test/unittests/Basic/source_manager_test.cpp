/// source_manager_test.cpp — SourceManager's coordinate-space overflow guard
///
/// A SourceLocation is a 32-bit offset into one coordinate space every
/// buffer shares, so a buffer (or a run of them) big enough to push
/// NextBase past UINT32_MAX would silently wrap and alias two different
/// positions.  wouldOverflow is what addBuffer checks before that can
/// happen; tested here with hypothetical sizes rather than by actually
/// allocating gigabytes of text, since the check itself is pure arithmetic
/// on the size a buffer WOULD be.
///
/// A deliberate, permanent GoogleTest exception (issue #43's GTest->lit
/// migration): there is no CLI path that could trigger this at all, short
/// of actually compiling a multi-gigabyte source file to make NextBase
/// approach UINT32_MAX, which no lit test in this repo does or should do.
/// wouldOverflow's own arithmetic has no Pascal-source-observable trigger
/// even in principle.

#include "plang/Basic/SourceManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <unistd.h>

using namespace plang;

TEST(SourceManagerOverflow, AnOrdinarySizedBufferDoesNotOverflow) {
    SourceManager SM;
    EXPECT_FALSE(SM.wouldOverflow(0));
    EXPECT_FALSE(SM.wouldOverflow(100));
    EXPECT_FALSE(SM.wouldOverflow(1'000'000));
}

TEST(SourceManagerOverflow, ABufferFillingTheRemainingSpaceExactlyFits) {
    // NextBase starts at 1 (0 is reserved for "nowhere"), and addBuffer
    // reserves one more past the end of the text for the EOF position --
    // see addBuffer's own "Hence the +1" comment -- so the largest buffer
    // that still fits leaves NextBase at exactly UINT32_MAX, not past it.
    SourceManager SM;
    const uint64_t Fits = std::numeric_limits<unsigned>::max() - 2;
    EXPECT_FALSE(SM.wouldOverflow(static_cast<size_t>(Fits)));
}

TEST(SourceManagerOverflow, OneByteMoreOverflows) {
    SourceManager SM;
    const uint64_t Overflows = std::numeric_limits<unsigned>::max() - 1;
    EXPECT_TRUE(SM.wouldOverflow(static_cast<size_t>(Overflows)));
}

TEST(SourceManagerOverflow, ASizeAtTheTypesOwnLimitOverflows) {
    SourceManager SM;
    EXPECT_TRUE(SM.wouldOverflow(std::numeric_limits<unsigned>::max()));
}

// addBuffer's own integration of the guard ("if (wouldOverflow(...))
// return nullopt") is deliberately not exercised end to end here: the only
// way to make it actually refuse is a Text whose real size overflows, and
// SourceManager keeps every buffer's text for the rest of the process, so
// there is no way to approach the boundary -- one huge buffer or many
// smaller ones summing to it -- without genuinely holding several
// gigabytes of memory at once.  The arithmetic above is what addBuffer
// defers to for the decision; this is where that arithmetic is checked.
//
// addFile's own integration (issue #218) is a different story, and IS
// exercised end to end below: unlike addBuffer, which only ever sees a
// Text already sitting in memory, addFile reads its own Text from a path,
// which means the size it must reject can be a lie -- std::filesystem::
// resize_file (what `truncate -s` also does, including in issue #218's own
// repro) moves a file's end-of-file marker without writing a single byte
// of it, so stat reports a multi-gigabyte size for a file that costs
// nothing to create and nothing to reject unread.
//
// That "unread" is exactly what regressed: addFile used to slurp the whole
// file into a std::string and only then hand its size to addBuffer, so
// wouldOverflow never got a say until the multi-gigabyte allocation it
// exists to prevent had already happened -- under this project's
// -fno-exceptions build, that allocation failing under memory pressure is
// std::terminate, not a diagnostic. addFile now stats the path and
// consults wouldOverflow before opening it at all (see SourceManager.cpp),
// so the multi-gigabyte read this test guards against never starts; the
// Elapsed check below is what would catch it if that ever regressed.
TEST(SourceManagerAddFile, AFileTooLargeToFitIsRejectedWithoutBeingRead) {
    char Tmpl[] = "/tmp/plang_test_XXXXXX";
    const int Fd = mkstemp(Tmpl);
    ASSERT_GE(Fd, 0);
    close(Fd);
    const std::string Path = Tmpl;

    std::error_code Ec;
    std::filesystem::resize_file(Path, std::numeric_limits<unsigned>::max(), Ec);
    ASSERT_FALSE(Ec) << Ec.message();

    SourceManager SM;
    const auto Start  = std::chrono::steady_clock::now();
    const auto Result = SM.addFile(Path);
    const auto Elapsed = std::chrono::steady_clock::now() - Start;

    std::remove(Path.c_str());

    EXPECT_FALSE(Result.has_value());
    // A stat call and an early return is microseconds of work. Actually
    // reading a file this size -- what the pre-fix addFile did before
    // reporting the very same rejection -- measured over a second on a
    // 2026 workstation with the file cached entirely in tmpfs, and only
    // gets slower on real disk or a busier machine. Half a second is a
    // wide margin on the fast side and nowhere close to the slow one.
    EXPECT_LT(Elapsed, std::chrono::milliseconds(500))
        << "addFile took long enough to suggest it read the file before "
           "checking its size, not after";
}
