#pragma once

#include "plang/Basic/SourceLocation.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Owns the text of every source buffer and answers questions about positions
/// in it.
///
/// Each buffer is given a contiguous stretch of one coordinate space, so a
/// SourceLocation is a single number that says both which buffer and where in
/// it.  Offset 0 is reserved to mean "nowhere", which is what an invalid
/// SourceLocation holds.
///
/// The manager keeps the text after scanning is over.  That is what lets a
/// diagnostic quote the line it is complaining about, and it costs one copy of
/// each file rather than one copy of the filename per token.
class SourceManager {
public:
    /// Take a buffer of text under the given name and return its FileID, or
    /// nullopt if there is no room left in the coordinate space (see
    /// wouldOverflow).  Reporting that is the caller's business, since the
    /// manager has no diagnostics engine.
    std::optional<FileID> addBuffer(std::string Name, std::string Text);

    /// Read a file and take its contents as a buffer, or return nullopt if it
    /// cannot be opened, or for the same reason addBuffer can fail.  The
    /// file's size is stat'd and checked against wouldOverflow before
    /// anything is read, so a file too large to ever fit is rejected without
    /// the read (and the allocation it would take) that addBuffer's own
    /// check alone cannot prevent.
    std::optional<FileID> addFile(const std::string& Path);

    /// Whether a buffer of \p TextSize bytes would run the coordinate space
    /// past what a 32-bit SourceLocation can address.  Exposed so a caller
    /// that can tell "not found" from "too large" apart some other way --
    /// Scanner's file-path constructor, which knows addFile fails either
    /// before opening the file (too large) or because it could not be
    /// opened at all (not found), but not which -- can report the right one.
    [[nodiscard]] bool wouldOverflow(size_t TextSize) const;

    /// The location of byte \p Offset within \p FID.  An offset at or past the
    /// end of the buffer is clamped to the end, which is where an
    /// end-of-file token sits.
    [[nodiscard]] SourceLocation getLocForOffset(FileID FID, size_t Offset) const;

    /// Which buffer a location is in; invalid if the location is.
    [[nodiscard]] FileID getFileID(SourceLocation Loc) const;

    /// Resolve a location into a filename, line and column.
    [[nodiscard]] PresumedLoc getPresumedLoc(SourceLocation Loc) const;

    /// The whole text of a buffer.
    [[nodiscard]] std::string_view getBufferData(FileID FID) const;

    /// The name a buffer was added under.
    [[nodiscard]] std::string_view getBufferName(FileID FID) const;

    /// The text of the line containing \p Loc, without its terminator.
    /// Empty if the location is invalid.  This is what a caret is drawn under.
    [[nodiscard]] std::string_view getLineText(SourceLocation Loc) const;

    [[nodiscard]] size_t numBuffers() const { return Buffers.size(); }

private:
    struct Buffer {
        std::string Name;
        std::string Text;
        /// First value in this buffer's stretch of the coordinate space.
        unsigned    Base{0};
        /// Byte offset of the start of each line; LineStarts[0] is always 0,
        /// so line N is at index N-1.
        std::vector<unsigned> LineStarts;
    };

    /// The buffer a position falls in, or nullptr if it falls in none.
    [[nodiscard]] const Buffer* bufferFor(SourceLocation Loc) const;
    [[nodiscard]] const Buffer* bufferFor(FileID FID) const;
    /// 1-based index of a buffer, which is what a FileID holds; 0 if not found.
    [[nodiscard]] unsigned indexOf(const Buffer* B) const;

    /// A deque and not a vector: the Scanner holds a string_view into a
    /// buffer's text, and a vector may move its elements when it grows.
    std::deque<Buffer> Buffers;
    /// Where the next buffer starts.  Begins at 1, leaving 0 to mean nowhere.
    unsigned NextBase{1};
};

} // namespace plang
