#pragma once

/// SwitchTable.h — what a compiler switch was set to at a place in the source
///
/// Turbo Pascal's switches are textual and positional: `{$R+}` written halfway
/// down a procedure applies to the rest of the file, including the main
/// program after it, and a program may turn a check off around one loop and on
/// again afterwards.  So "is range checking on" is a question about a source
/// location, not about the compilation, and there is nowhere on LangOptions to
/// answer it.
///
/// Three designs were proposed for this.  Snapshotting the state onto every
/// AST node costs bytes on every node and makes the AST printer and the
/// interface writer learn about switches to stay correct; a map keyed by
/// something coarser cannot answer `{$IFOPT}` at all.  A table of the points
/// where the state changed costs one entry per directive, is queried by binary
/// search, and answers `{$IFOPT}` by construction.
///
/// A null table means no directive was ever seen, and every query returns the
/// caller's default.  That is what keeps ISO 7185 and Extended Pascal
/// byte-identical: they have no directives, so they never build a table, so
/// they never take a different path here.

#include "plang/Basic/SourceLocation.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace plang {

/// Which switches there are.  One bit each in a CompilerState.
enum class Switch : unsigned {
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) Id,
#include "plang/Basic/CompilerSwitches.def"
};

inline constexpr unsigned NumSwitches = 0
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) + 1
#include "plang/Basic/CompilerSwitches.def"
    ;

static_assert(NumSwitches <= 16, "CompilerState holds one bit per switch");

/// The state of every switch at one point, as a bitmask.
class CompilerState {
public:
    constexpr CompilerState() = default;
    explicit constexpr CompilerState(uint16_t Bits) : B(Bits) {}

    [[nodiscard]] constexpr bool on(Switch S) const {
        return (B & bit(S)) != 0;
    }
    constexpr void set(Switch S, bool On) {
        if (On) B |= bit(S); else B = static_cast<uint16_t>(B & ~bit(S));
    }
    [[nodiscard]] constexpr uint16_t bits() const { return B; }

    friend constexpr bool operator==(CompilerState, CompilerState) = default;

    /// What Turbo Pascal starts a compilation with.
    static constexpr CompilerState turboDefaults() {
        CompilerState C;
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) \
        C.set(Switch::Id, TurboDefault);
#include "plang/Basic/CompilerSwitches.def"
        return C;
    }

private:
    static constexpr uint16_t bit(Switch S) {
        return static_cast<uint16_t>(1u << static_cast<unsigned>(S));
    }
    uint16_t B{0};
};

/// The letter `{$R+}` spells \p S with.
[[nodiscard]] constexpr char switchLetter(Switch S) {
    switch (S) {
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) \
    case Switch::Id: return Letter;
#include "plang/Basic/CompilerSwitches.def"
    }
    return '\0';
}

/// The word `{$RANGECHECKS ON}` spells \p S with, lower case.
[[nodiscard]] constexpr std::string_view switchLongName(Switch S) {
    switch (S) {
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) \
    case Switch::Id: return LongName;
#include "plang/Basic/CompilerSwitches.def"
    }
    return "";
}

/// Whether plang acts on \p S, as opposed to recording it so that `{$IFOPT}`
/// can answer truthfully.
[[nodiscard]] constexpr bool switchIsHonored(Switch S) {
    switch (S) {
#define SWITCH(Id, Letter, LongName, TurboDefault, Honored) \
    case Switch::Id: return Honored;
#include "plang/Basic/CompilerSwitches.def"
    }
    return false;
}

/// The switch \p Letter names, or nothing.  Case insensitive, since a program
/// may write either.
[[nodiscard]] constexpr std::optional<Switch> switchFromLetter(char Letter) {
    const char Lower = (Letter >= 'A' && Letter <= 'Z')
                           ? static_cast<char>(Letter - 'A' + 'a') : Letter;
#define SWITCH(Id, Letter_, LongName, TurboDefault, Honored) \
    if (Lower == (Letter_)) return Switch::Id;
#include "plang/Basic/CompilerSwitches.def"
    return std::nullopt;
}

/// The switch \p Name names, or nothing.  \p Name must already be lower case;
/// every caller has folded it to look it up in some other table first.
[[nodiscard]] constexpr std::optional<Switch> switchFromLongName(std::string_view Name) {
#define SWITCH(Id, Letter, LongName_, TurboDefault, Honored) \
    if (Name == (LongName_)) return Switch::Id;
#include "plang/Basic/CompilerSwitches.def"
    return std::nullopt;
}

/// Where the switch state changed, in source order.
///
/// Built by the scanner as it reads directives, then read by everything
/// downstream.  Nothing mutates it after the front end has finished, which is
/// why it is handed around as a shared_ptr to const.
class SwitchTable {
public:
    /// Records that from \p Loc onwards the state is \p State.
    ///
    /// Locations usually arrive in nondecreasing order -- that is the order
    /// ordinary, single-buffer scanning produces them in, and the common case
    /// this stays an O(1) append for.  They do NOT arrive that way across an
    /// `{$I file}`/`{$INCLUDE file}` boundary: an included buffer occupies a
    /// *later* stretch of the shared coordinate space than the file that
    /// included it (SourceManager lays buffers out in the order they are
    /// opened, not the order their text is read), so the point the scanner
    /// records on RESUMING the includer -- see openInclude/popInclude in
    /// Lex/Directives.cpp -- has a *smaller* raw offset than whatever the
    /// include's own last point was, even though it comes later in read
    /// order.  A plain "replace the back element when the new point does not
    /// sort after it" (this method's previous shape) silently discarded that
    /// last in-include point instead of inserting the resume point where it
    /// actually belongs, corrupting every query for a location later in the
    /// include than its own last switch directive.  Insert in sorted
    /// position instead: the common case is still one comparison and a
    /// push_back, and the out-of-order case -- bounded by how many `{$...}`
    /// switch directives one file has, never large -- costs a linear
    /// shift instead of silent data loss.  A second point at the same
    /// location still replaces the first: two directives in one comment
    /// settle to their combined effect, and only their combined effect is
    /// reachable.
    void record(SourceLocation Loc, CompilerState State) {
        const auto It = std::lower_bound(
            Points.begin(), Points.end(), Loc.raw(),
            [](const Point& P, unsigned Raw) { return P.Loc.raw() < Raw; });
        if (It != Points.end() && It->Loc.raw() == Loc.raw()) {
            It->State = State;
            return;
        }
        Points.insert(It, Point{Loc, State});
    }

    /// The state in force at \p Loc, or \p Default where nothing was recorded
    /// before it.
    ///
    /// Locations are offsets into one coordinate space covering every buffer,
    /// and an included file occupies a *later* stretch of it than the file that
    /// included it — so a plain search backwards from \p Loc would find the
    /// includer's state for text that comes after the include and the included
    /// file's state for text that comes before it.  What keeps that right is
    /// the scanner recording an explicit point at offset zero of each buffer it
    /// pushes and at the parent's resume offset when it pops, which turns the
    /// question back into "the last point at or before here".
    [[nodiscard]] CompilerState stateAt(SourceLocation Loc,
                                        CompilerState Default) const {
        if (Points.empty() || Loc.isInvalid()) return Default;
        // The first point strictly after Loc; the one before it is the answer.
        const auto It = std::upper_bound(
            Points.begin(), Points.end(), Loc.raw(),
            [](unsigned Raw, const Point& P) { return Raw < P.Loc.raw(); });
        if (It == Points.begin()) return Default;
        return std::prev(It)->State;
    }

    /// Whether \p S is on at \p Loc, given what it is when no directive has
    /// said otherwise.
    [[nodiscard]] bool on(Switch S, SourceLocation Loc, CompilerState Default) const {
        return stateAt(Loc, Default).on(S);
    }

    [[nodiscard]] bool     empty() const { return Points.empty(); }
    [[nodiscard]] size_t   size()  const { return Points.size();  }

private:
    struct Point {
        SourceLocation Loc;
        CompilerState  State;
    };
    std::vector<Point> Points;
};

} // namespace plang
