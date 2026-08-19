/// source_manager_test.cpp — SourceManager's coordinate-space overflow guard
///
/// A SourceLocation is a 32-bit offset into one coordinate space every
/// buffer shares, so a buffer (or a run of them) big enough to push
/// NextBase past UINT32_MAX would silently wrap and alias two different
/// positions.  wouldOverflow is what addBuffer checks before that can
/// happen; tested here with hypothetical sizes rather than by actually
/// allocating gigabytes of text, since the check itself is pure arithmetic
/// on the size a buffer WOULD be.

#include "plang/Basic/SourceManager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

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
