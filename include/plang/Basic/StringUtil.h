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

} // namespace plang
