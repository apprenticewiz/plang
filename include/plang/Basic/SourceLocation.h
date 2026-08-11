#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace plang {

/// Identifies one source buffer held by a SourceManager.
class FileID {
public:
    constexpr FileID() = default;

    [[nodiscard]] constexpr bool     isValid() const { return ID != 0; }
    [[nodiscard]] constexpr bool     isInvalid() const { return ID == 0; }
    [[nodiscard]] constexpr unsigned raw() const { return ID; }

    static constexpr FileID fromRaw(unsigned R) {
        FileID F;
        F.ID = R;
        return F;
    }

    friend constexpr auto operator<=>(FileID, FileID) = default;

private:
    unsigned ID{0};
};

/// A position in the source, held as an opaque 32-bit value.
///
/// The value is an offset into the single coordinate space a SourceManager
/// lays every buffer out in, so the manager can recover the file, the line and
/// the column from it, and can find the text of the line to print under a
/// diagnostic.  Nothing else may take it apart.
///
/// It is four bytes because every token and every AST node carries one.  The
/// obvious alternative — a filename, a line and a column stored together —
/// costs forty bytes and a heap allocation per node, nearly all of it the same
/// filename over and over.
///
/// A default-constructed SourceLocation is invalid and means "nowhere",
/// which is what a node the parser synthesized has.
class SourceLocation {
public:
    constexpr SourceLocation() = default;

    [[nodiscard]] constexpr bool     isValid() const { return ID != 0; }
    [[nodiscard]] constexpr bool     isInvalid() const { return ID == 0; }
    [[nodiscard]] constexpr unsigned raw() const { return ID; }

    static constexpr SourceLocation fromRaw(unsigned R) {
        SourceLocation L;
        L.ID = R;
        return L;
    }

    /// Order by position, which for two locations in one buffer is source
    /// order.  Used to sort diagnostics.
    friend constexpr auto operator<=>(SourceLocation, SourceLocation) = default;

private:
    unsigned ID{0};
};

static_assert(sizeof(SourceLocation) == 4,
              "SourceLocation is stored in every AST node and must stay small");

/// A SourceLocation resolved into something a person can read.
///
/// "Presumed" is clang's word for it and the caution it carries is worth
/// keeping: this is where the text says it is, which a directive could in
/// principle move.  Pascal has no such directive, so for plang it is simply
/// where it is.
struct PresumedLoc {
    /// Name of the buffer, empty if the location is invalid.
    std::string Filename;
    /// 1-based; 0 when the location is invalid.
    unsigned    Line{0};
    /// 1-based; 0 when the location is invalid.
    unsigned    Column{0};

    [[nodiscard]] bool isValid() const { return Line != 0; }
};

} // namespace plang
