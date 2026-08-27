#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace plang {

/// Return a lowercase copy of S.  Pascal identifiers are case-insensitive;
/// use this (or eqCI below) instead of inlining tolower loops everywhere.
[[nodiscard]] inline std::string toLower(std::string_view S) {
    std::string R{S};
    for (auto& C : R)
        C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
    return R;
}

/// Return true if A and B are equal under case-insensitive comparison.
/// Intended for Pascal identifier comparison.
[[nodiscard]] inline bool eqCI(std::string_view A, std::string_view B) {
    if (A.size() != B.size()) return false;
    for (size_t I = 0; I < A.size(); ++I)
        if (std::tolower(static_cast<unsigned char>(A[I])) !=
            std::tolower(static_cast<unsigned char>(B[I])))
            return false;
    return true;
}

/// Return a copy of S with every C0 control byte and DEL rendered as a
/// visible \xHH escape instead of passed through as itself.
///
/// S is text plang did not choose: a filename from argv, or a locale tag
/// from $LANG/$LC_MESSAGES/-fdiagnostics-language=.  Both reach stderr (a
/// diagnostic's "file:line:col:" prefix, --version's "Messages: " line, a
/// -v/-### command echo) verbatim otherwise, and an attacker who controls
/// either can steer the terminal with it: hide or rewrite what is on
/// screen with an SGR or cursor-motion sequence, or -- with a bare \n --
/// split one diagnostic into what a log viewer reads as two.  This is the
/// same threat MessageCatalog.cpp's .po reader refuses raw control bytes
/// over (see its file comment), and the same threshold: C < 0x20 or
/// C == 0x7F.  Bytes above 0x7F pass through untouched -- they may be part
/// of a legitimate UTF-8 filename, and mangling them would be punishing the
/// common case for a threat that lives entirely below 0x20.
[[nodiscard]] inline std::string escapeControlChars(std::string_view S) {
    static const char Hex[] = "0123456789abcdef";
    std::string R;
    R.reserve(S.size());
    for (const unsigned char C : S) {
        if (C < 0x20 || C == 0x7F) {
            R += '\\'; R += 'x';
            R += Hex[C >> 4]; R += Hex[C & 0xF];
        } else {
            R += static_cast<char>(C);
        }
    }
    return R;
}

} // namespace plang
