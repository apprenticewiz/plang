//===- SourceManager.cpp - Ownership of source text and positions ---------===//

#include "plang/Basic/SourceManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

using namespace plang;

bool SourceManager::wouldOverflow(size_t TextSize) const {
    // +1: one past the end of the text is a valid position (see addBuffer).
    // Computed in 64 bits so the check itself cannot be the thing that
    // overflows -- NextBase is already this space's high-water mark, and
    // TextSize alone can exceed UINT32_MAX for a pathological input.
    const uint64_t Needed = static_cast<uint64_t>(NextBase)
                          + static_cast<uint64_t>(TextSize) + 1;
    return Needed > std::numeric_limits<unsigned>::max();
}

std::optional<FileID> SourceManager::addBuffer(std::string Name, std::string Text) {
    // A leading UTF-8 BOM (EF BB BF) is invisible in whatever editor wrote
    // it and isn't part of the Pascal source; drop it here, once, for every
    // buffer this SourceManager ever holds (file or in-memory), so the
    // scanner never sees it and every downstream offset/line/column is
    // computed on the real text.  Checked only at Text's own start (offset
    // 0 of *this* buffer), so the same three bytes elsewhere -- inside a
    // string literal, say -- are left alone.
    static constexpr std::string_view UTF8BOM = "\xEF\xBB\xBF";
    if (Text.starts_with(UTF8BOM))
        Text.erase(0, UTF8BOM.size());

    // A buffer big enough to wrap NextBase would silently alias two
    // different positions -- every SourceLocation past the wrap would
    // resolve into whatever buffer next claimed that coordinate range.
    if (wouldOverflow(Text.size())) return std::nullopt;

    Buffer B;
    B.Name = std::move(Name);
    B.Text = std::move(Text);
    B.Base = NextBase;

    // Index the line starts once, so that resolving a location later is a
    // binary search rather than a scan from the top of the file.
    //
    // \n and \r\n both end a line at the byte right after the \n, same as
    // before #285.  A bare \r -- classic Mac OS's own line ending, with no
    // \n anywhere -- ends a line too: without this, a file that uses it
    // exclusively has no \n at all, so the loop below would never push a
    // second entry and every diagnostic in the file would resolve to line
    // 1, with getLineText handing back the entire file as "the line". When
    // a \r is immediately followed by \n, they are one terminator, not two:
    // counting them separately would insert a spurious empty line between
    // every real line of a CRLF file.
    B.LineStarts.push_back(0);
    for (size_t I = 0; I < B.Text.size(); ++I) {
        if (B.Text[I] == '\n') {
            B.LineStarts.push_back(static_cast<unsigned>(I + 1));
        } else if (B.Text[I] == '\r') {
            if (I + 1 < B.Text.size() && B.Text[I + 1] == '\n') ++I;
            B.LineStarts.push_back(static_cast<unsigned>(I + 1));
        }
    }

    // One past the end of the text is a valid position: it is where the
    // end-of-file token sits.  Hence the +1.
    NextBase += static_cast<unsigned>(B.Text.size()) + 1;

    Buffers.push_back(std::move(B));
    return FileID::fromRaw(static_cast<unsigned>(Buffers.size()));
}

std::optional<FileID> SourceManager::addFile(const std::string& Path) {
    // Stat before opening anything: wouldOverflow's whole job is to keep a
    // pathological input from being read into memory at all, and a file big
    // enough to trip it (issue #218) is exactly the file that must never
    // reach the read below.  Checking Ss.str().size() only after `Ss <<
    // File.rdbuf()` had already read the whole thing -- what this once did --
    // means the multi-gigabyte allocation the check exists to prevent has
    // already happened by the time it runs; under this project's
    // -fno-exceptions build that allocation failing is std::terminate, not a
    // diagnostic. A stat failure (file missing, unreadable, race with a
    // deletion) is left for the ifstream open below to report the usual way,
    // since Ec alone cannot tell "doesn't exist" apart from "exists but its
    // size is unknown for some other reason" and both are already handled
    // there.
    std::error_code Ec;
    const std::uintmax_t Size = std::filesystem::file_size(Path, Ec);
    if (!Ec && wouldOverflow(static_cast<size_t>(Size))) return std::nullopt;

    std::ifstream File(Path);
    if (!File) return std::nullopt;
    std::ostringstream Ss;
    Ss << File.rdbuf();
    return addBuffer(Path, Ss.str());
}

const SourceManager::Buffer* SourceManager::bufferFor(FileID FID) const {
    if (FID.isInvalid() || FID.raw() > Buffers.size()) return nullptr;
    return &Buffers[FID.raw() - 1];
}

const SourceManager::Buffer* SourceManager::bufferFor(SourceLocation Loc) const {
    if (Loc.isInvalid()) return nullptr;
    // The buffer whose Base is the greatest one not exceeding the location.
    auto It = std::upper_bound(Buffers.begin(), Buffers.end(), Loc.raw(),
                               [](unsigned V, const Buffer& B) { return V < B.Base; });
    if (It == Buffers.begin()) return nullptr;
    --It;
    if (Loc.raw() > It->Base + It->Text.size()) return nullptr;
    return &*It;
}

unsigned SourceManager::indexOf(const Buffer* B) const {
    for (size_t I = 0; I < Buffers.size(); ++I)
        if (&Buffers[I] == B) return static_cast<unsigned>(I) + 1;
    return 0;
}

SourceLocation SourceManager::getLocForOffset(FileID FID, size_t Offset) const {
    const Buffer* B = bufferFor(FID);
    if (!B) return {};
    if (Offset > B->Text.size()) Offset = B->Text.size();
    return SourceLocation::fromRaw(B->Base + static_cast<unsigned>(Offset));
}

FileID SourceManager::getFileID(SourceLocation Loc) const {
    return FileID::fromRaw(indexOf(bufferFor(Loc)));
}

PresumedLoc SourceManager::getPresumedLoc(SourceLocation Loc) const {
    const Buffer* B = bufferFor(Loc);
    if (!B) return {};

    const unsigned Off = Loc.raw() - B->Base;
    // The last line whose start is at or before the offset.
    auto It = std::upper_bound(B->LineStarts.begin(), B->LineStarts.end(), Off);
    const size_t   Idx   = static_cast<size_t>(It - B->LineStarts.begin()) - 1;
    const unsigned Start = B->LineStarts[Idx];

    // The column a terminal would show, not a byte count (#285): every
    // UTF-8 continuation byte between the line's start and Off belongs to a
    // character already counted by its lead byte, so it does not get a
    // column of its own.  For an all-ASCII line (the common case) every
    // byte is a lead byte, so this counts exactly the same as the plain
    // `Off - Start` it replaces.
    //
    // Advances by whole VALIDATED sequences (#614), not by classifying each
    // byte on its own: an isolated/malformed continuation byte with no valid
    // lead byte before it is not part of any character and must occupy a
    // display cell of its own, or every column after it comes out one too
    // low.
    unsigned Column = 1;
    for (unsigned I = Start; I < Off; ++Column) {
        const unsigned Len = utf8SequenceLength(B->Text, I);
        I += Len;
    }

    return PresumedLoc{ B->Name, static_cast<unsigned>(Idx) + 1, Column };
}

std::string_view SourceManager::getBufferData(FileID FID) const {
    const Buffer* B = bufferFor(FID);
    return B ? std::string_view(B->Text) : std::string_view{};
}

std::string_view SourceManager::getBufferName(FileID FID) const {
    const Buffer* B = bufferFor(FID);
    return B ? std::string_view(B->Name) : std::string_view{};
}

std::string_view SourceManager::getLineText(SourceLocation Loc) const {
    const Buffer* B = bufferFor(Loc);
    if (!B) return {};

    const unsigned Off = Loc.raw() - B->Base;
    auto It = std::upper_bound(B->LineStarts.begin(), B->LineStarts.end(), Off);
    const size_t   Idx   = static_cast<size_t>(It - B->LineStarts.begin()) - 1;
    const unsigned Start = B->LineStarts[Idx];
    const unsigned End   = (Idx + 1 < B->LineStarts.size())
                               ? B->LineStarts[Idx + 1] - 1  // drop the '\n'
                               : static_cast<unsigned>(B->Text.size());

    std::string_view Line(B->Text);
    Line = Line.substr(Start, End - Start);
    if (!Line.empty() && Line.back() == '\r') Line.remove_suffix(1);
    return Line;
}
