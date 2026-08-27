// driver_test.cpp -- in-process regression tests for plang::Driver
// properties with no CLI-observable proxy.
//
// Two independent, deliberate, permanent GoogleTest exceptions to the
// project's lit-first policy for test/Driver/ (issue #34: the GTest suite
// that used to live here was migrated there wholesale, and nothing was
// meant to return) -- both earn the exception on the same grounds as
// test/unittests/Basic/catalog_test.cpp's own header comment gives for its
// permanent GoogleTest cases: the property under test has no CLI-observable
// proxy at all.
//
// DriverInProcessDeathTest (issue #174): Driver.h documents run() as
// returning a Unix exit code to its caller, but parseArgs used to call
// std::exit() directly for --version, --help, -dumpversion, -dumpmachine,
// and --help-warnings. Running plang as a subprocess -- what every
// test/Driver/ lit case does, and the only way this project's own CLI is
// ever actually invoked -- cannot tell that apart from a normal return: the
// exit code and everything printed are identical either way, since
// std::exit() reports the same code std::exit()'s caller would have
// returned anyway. The difference only matters to a caller that links
// PlangDriver and calls Driver::run() in-process instead of spawning plang
// -- std::exit() there tears down that host's whole process out from under
// it rather than returning control the way Driver.h promises. With no
// CLI-observable proxy for that distinction, this can only be tested
// in-process -- exactly what issue #174's own suggested direction asked for
// ("add in-process tests").
//
// VersionDirLess (issue #250): detectGCC() and builtinsUnder() pick the
// newest of several installed toolchain versions by sorting directory names
// (e.g. "9", "10", "12.2.0") and taking the last one (see
// lib/Driver/Driver.cpp), but exercising that end-to-end would need the
// test machine to actually have multiple numbered GCC/Clang installs laid
// out in a specific, pathological order (9 next to 10, or 2 next to 10) --
// not something a hermetic CI environment can be relied on to have. The
// comparator itself takes no filesystem input, so it is tested directly.
// subdirs() used to sort with plain std::string comparison, so "10" < "9"
// and "10.1.0" < "9.5.0" lexicographically -- the reverse iteration in
// detectGCC/builtinsUnder that is supposed to land on the newest version
// landed on an arbitrary one instead (wrong once a directory name reaches
// two digits, latent otherwise).

#include "plang/Driver/Driver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace plang;

namespace {

/// Runs \p Args through Driver::run() and, only if that call returns
/// control normally, prints a marker naming the return code and exits with
/// it. Meant to run inside EXPECT_EXIT's forked child: if Driver::run()
/// instead calls std::exit() itself (the bug), the child is gone before the
/// marker line is ever reached.
void runInChildAndReport(std::vector<std::string> Args) {
    std::vector<char *> Argv;
    Argv.push_back(const_cast<char *>("plang"));
    for (std::string &A : Args) Argv.push_back(A.data());

    testing::internal::CaptureStdout(); // keep --version/--help's own banner
                                         // out of the test log; irrelevant here
    const int Rc = plang::Driver(nullptr).run(static_cast<int>(Argv.size()), Argv.data());
    testing::internal::GetCapturedStdout();

    std::fprintf(stderr, "DRIVER-RUN-RETURNED rc=%d\n", Rc);
    std::exit(Rc);
}

void expectRunReturnsZero(std::vector<std::string> Args) {
    EXPECT_EXIT(runInChildAndReport(std::move(Args)),
                ::testing::ExitedWithCode(0),
                "DRIVER-RUN-RETURNED rc=0");
}

/// Sorts Dirs ascending with versionDirLess, the same call subdirs() makes,
/// and returns the result -- so a test can assert on the whole order at once
/// instead of one pairwise comparison at a time.
std::vector<std::string> sorted(std::vector<std::string> Dirs) {
    std::sort(Dirs.begin(), Dirs.end(), versionDirLess);
    return Dirs;
}

} // namespace

// EXPECT_EXIT is doing double duty here, not just its usual "does this
// terminate the process" job. runInChildAndReport()'s statement always
// finishes by calling std::exit() itself, with a marker already written to
// stderr -- so the *only* way it can fail to reach that marker is if
// Driver::run() exited the child first. A plain (non-death) EXPECT_EQ on
// run()'s return value cannot make this distinction on its own: a child
// that never came back from run() because parseArgs called std::exit(0)
// partway through it, and a child that returned normally with rc == 0,
// both look like "exited with code 0" to whatever ran them -- one of them
// just never reached the assertion that would have said so.

TEST(DriverInProcessDeathTest, VersionReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--version"});
}

TEST(DriverInProcessDeathTest, DumpversionReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"-dumpversion"});
}

TEST(DriverInProcessDeathTest, DumpmachineReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"-dumpmachine"});
}

TEST(DriverInProcessDeathTest, HelpReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--help"});
}

TEST(DriverInProcessDeathTest, HelpWarningsReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--help-warnings"});
}

// run()'s other, non-early-exit return path (compile() has always returned
// an ordinary int) was never in question, but it is cheap to confirm the
// informational-action fix did not somehow change it too: no arguments at
// all is diag::err_no_input_files, rc == 1, and always was a normal return
// rather than a std::exit() -- included as a control alongside the five
// informational-action cases above, all of which the fix changed from
// std::exit() to a normal return with the same rc == 0 they always had.
TEST(DriverInProcessDeathTest, NoInputFilesReturnsOneInsteadOfExiting) {
    EXPECT_EXIT(runInChildAndReport({}),
                ::testing::ExitedWithCode(1),
                "DRIVER-RUN-RETURNED rc=1");
}

// ---------------------------------------------------------------------------
// The exact scenario issue #250 reports
// ---------------------------------------------------------------------------

TEST(VersionDirLess, DoubleDigitMinorSortsAfterSingleDigitMajor) {
    // "10.1.0" < "9.5.0" lexicographically (since '1' < '9'), which used to
    // make detectGCC()/builtinsUnder() -- both of which take the *last*
    // element after an ascending sort -- prefer the older 9.5.0 toolchain.
    EXPECT_FALSE(versionDirLess("10.1.0", "9.5.0"));
    EXPECT_TRUE(versionDirLess("9.5.0", "10.1.0"));

    const auto Order = sorted({"10.1.0", "9.5.0"});
    ASSERT_EQ(Order.size(), 2u);
    EXPECT_EQ(Order.back(), "10.1.0") << "newest-last order should survive the sort";
}

TEST(VersionDirLess, DoubleDigitMajorSortsAfterSingleDigit) {
    // The task's own example layout: .../gcc/x86_64-linux-gnu/{9,12,13}.
    const auto Order = sorted({"12", "9", "13"});
    ASSERT_EQ(Order.size(), 3u);
    EXPECT_EQ(Order, (std::vector<std::string>{"9", "12", "13"}));
    EXPECT_EQ(Order.back(), "13");
}

// ---------------------------------------------------------------------------
// Pairwise sanity: numeric, not lexicographic
// ---------------------------------------------------------------------------

TEST(VersionDirLess, SingleDigitBeforeDoubleDigit) {
    EXPECT_TRUE(versionDirLess("9", "10"));
    EXPECT_FALSE(versionDirLess("10", "9"));
}

TEST(VersionDirLess, TwoBeforeTen) {
    // The other lexicographic trap named in the issue: "2" > "10" as strings.
    EXPECT_TRUE(versionDirLess("2", "10"));
    EXPECT_FALSE(versionDirLess("10", "2"));
}

TEST(VersionDirLess, EqualVersionsAreNeitherLess) {
    EXPECT_FALSE(versionDirLess("12.2.0", "12.2.0"));
}

TEST(VersionDirLess, LatentCaseAlreadySortedLexicographicallyStillWorks) {
    // 14 < 15 < 16 both numerically and lexicographically -- the bug is
    // latent here, but the comparator must still get it right.
    EXPECT_TRUE(versionDirLess("14", "15"));
    EXPECT_TRUE(versionDirLess("15", "16"));
}

// ---------------------------------------------------------------------------
// Directory names that are not version numbers at all
// ---------------------------------------------------------------------------

TEST(VersionDirLess, AnUnparseableNameSortsBeforeAnyRealVersion) {
    // So a stray non-version directory next to real ones is never mistaken
    // for the newest install by a caller that takes the last element.
    EXPECT_TRUE(versionDirLess("current", "9"));
    EXPECT_FALSE(versionDirLess("9", "current"));

    const auto Order = sorted({"12", "current", "9"});
    EXPECT_EQ(Order, (std::vector<std::string>{"current", "9", "12"}));
    EXPECT_EQ(Order.back(), "12") << "a non-version entry must not be picked as newest";
}

TEST(VersionDirLess, TwoUnparseableNamesFallBackToLexicographicOrder) {
    EXPECT_TRUE(versionDirLess("a", "b"));
    EXPECT_FALSE(versionDirLess("b", "a"));
}
