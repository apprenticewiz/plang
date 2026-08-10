//===- ParserInternal.h - Helpers shared by the Parse*.cpp files ===//
//
// Definitions needed by more than one of the parser's translation units.
// Anything used by only one of them stays file-static in that file.
//
//===----------------------------------------------------------------===//
#pragma once

#include <string>

namespace plang {

// ---------------------------------------------------------------------------
// Label declarations
// ---------------------------------------------------------------------------

// ISO §6.1.6: "Labels shall be digit-sequences and shall be distinguished by
// their apparent integral values ... The spelling of a label shall be its
// apparent integral value."  `003`, `03` and `3` are therefore one label, whose
// spelling is `3`.
//
// Reducing it to that here is what makes the rest of the compiler agree, since
// everything downstream keys on this string: the symbol table the label section
// fills, the lookup a goto does in it, and the name of the basic block the label
// becomes.  Three spellings of the one label made three of each, so a program
// declaring `003` and jumping to `3` had both an undefined label and a label
// never defined.
//
// A label that is not all digits is not a label at all; Sema is where that is
// said, so it passes through unchanged to be reported there.
inline std::string canonicalLabel(const std::string& Lexeme) {
    for (char C : Lexeme)
        if (!std::isdigit(static_cast<unsigned char>(C))) return Lexeme;
    const auto First = Lexeme.find_first_not_of('0');
    return First == std::string::npos ? "0" : Lexeme.substr(First);
}

} // namespace plang
