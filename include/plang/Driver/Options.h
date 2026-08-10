#pragma once

#include <string>
#include <string_view>

namespace plang::opts {

/// How an option's value, if it has one, is attached to it.
enum class Kind {
    Flag,             ///< no value:            -g
    Joined,           ///< glued on:            -std=iso7185
    Separate,         ///< the next argument:   -o out.s
    JoinedOrSeparate, ///< either:              -Idir  or  -I dir
};

/// Who acts on an option.
enum class Consumer {
    Driver,   ///< the driver alone
    Frontend, ///< passed through to -pc1
    Both,     ///< both act on it, and the driver passes it through
};

struct Option {
    std::string_view Spelling;
    std::string_view Display;
    Kind             OptKind;
    Consumer         For;
    std::string_view Help;
};

inline constexpr Option Table[] = {
#define OPTION(Spelling, Display, OptKind, For, Help)                          \
    { Spelling, Display, Kind::OptKind, Consumer::For, Help },
#include "plang/Driver/Options.def"
};

/// The option \p Arg names, or nullptr if no option matches it.
///
/// Longest spelling wins, so that -Wno-x is not mistaken for -W with a value
/// of "no-x", and -Wl,foo goes to the linker rather than the warning machinery.
[[nodiscard]] inline const Option* lookup(std::string_view Arg) {
    const Option* Best = nullptr;
    for (const Option& O : Table) {
        const bool Matches = (O.OptKind == Kind::Joined)
                                 ? Arg.starts_with(O.Spelling)
                                 : Arg == O.Spelling ||
                                       (O.OptKind == Kind::JoinedOrSeparate &&
                                        Arg.starts_with(O.Spelling));
        if (!Matches) continue;
        if (!Best || O.Spelling.size() > Best->Spelling.size()) Best = &O;
    }
    return Best;
}

/// True if the option needs the argument after it: -o out.s but not -oout.s.
[[nodiscard]] inline bool takesSeparateValue(const Option& O,
                                             std::string_view Arg) {
    if (O.OptKind == Kind::Separate) return true;
    if (O.OptKind != Kind::JoinedOrSeparate) return false;
    return Arg.size() == O.Spelling.size(); // nothing glued on, so it follows
}

/// True if the driver should hand this option to the front end.
[[nodiscard]] inline bool goesToFrontend(const Option& O) {
    return O.For == Consumer::Frontend || O.For == Consumer::Both;
}

/// Render the OPTIONS section of a help message, listing the options \p Want
/// covers.  Options with no Display or no Help are left out, which is how an
/// alias such as --help is kept from being listed twice.
[[nodiscard]] inline std::string helpText(Consumer Want) {
    std::string Out;
    for (const Option& O : Table) {
        if (O.Display.empty() || O.Help.empty()) continue;
        if (Want != Consumer::Both && O.For != Want && O.For != Consumer::Both)
            continue;
        Out += "  ";
        Out += O.Display;
        // Pad the description out to a common column, or start a new line if
        // the spelling has already run past it.
        constexpr size_t Col = 21;
        if (O.Display.size() + 2 < Col) Out.append(Col - O.Display.size() - 2, ' ');
        else                            Out += "\n" + std::string(Col, ' ');
        Out += O.Help;
        Out += "\n";
    }
    return Out;
}

} // namespace plang::opts
