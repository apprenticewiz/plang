/// driver_test.cpp -- ordering version-numbered toolchain directories
///
/// A deliberate, permanent GoogleTest exception to the project's lit-first
/// policy for test/Driver/ (issue #34: the GTest suite that used to live at
/// test/unittests/Driver/ was migrated there wholesale, and nothing was
/// meant to return).  versionDirLess earns the exception on the same grounds
/// as MessageCatalog's catalogSearchOrder (test/unittests/Basic/catalog_test.cpp):
/// it is a pure function with no CLI-observable proxy. detectGCC() and
/// builtinsUnder() pick the newest of several installed toolchain versions by
/// sorting directory names (e.g. "9", "10", "12.2.0") and taking the last one
/// (see lib/Driver/Driver.cpp), but exercising that end-to-end would need the
/// test machine to actually have multiple numbered GCC/Clang installs laid
/// out in a specific, pathological order (9 next to 10, or 2 next to 10) --
/// not something a hermetic CI environment can be relied on to have. The
/// comparator itself takes no filesystem input, so it is tested directly.
///
/// Issue #250: subdirs() used to sort with plain std::string comparison, so
/// "10" < "9" and "10.1.0" < "9.5.0" lexicographically -- the reverse
/// iteration in detectGCC/builtinsUnder that is supposed to land on the
/// newest version landed on an arbitrary one instead (wrong once a
/// directory name reaches two digits, latent otherwise).

#include "plang/Driver/Driver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace plang;

namespace {

/// Sorts Dirs ascending with versionDirLess, the same call subdirs() makes,
/// and returns the result -- so a test can assert on the whole order at once
/// instead of one pairwise comparison at a time.
std::vector<std::string> sorted(std::vector<std::string> Dirs) {
    std::sort(Dirs.begin(), Dirs.end(), versionDirLess);
    return Dirs;
}

} // namespace

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
