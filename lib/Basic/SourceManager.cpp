//===- SourceManager.cpp - Ownership of source text and positions ---------===//

#include "plang/Basic/SourceManager.h"

#include <algorithm>
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
    B.LineStarts.push_back(0);
    for (size_t I = 0; I < B.Text.size(); ++I)
        if (B.Text[I] == '\n') B.LineStarts.push_back(static_cast<unsigned>(I + 1));

    // One past the end of the text is a valid position: it is where the
    // end-of-file token sits.  Hence the +1.
    NextBase += static_cast<unsigned>(B.Text.size()) + 1;

    Buffers.push_back(std::move(B));
    return FileID::fromRaw(static_cast<unsigned>(Buffers.size()));
}

std::optional<FileID> SourceManager::addFile(const std::string& Path) {
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
    const size_t Idx = static_cast<size_t>(It - B->LineStarts.begin()) - 1;

    return PresumedLoc{ B->Name,
                        static_cast<unsigned>(Idx) + 1,
                        Off - B->LineStarts[Idx] + 1 };
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
