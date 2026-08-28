//===- ParserInternal.h - Helpers shared by the Parse*.cpp files ===//
//
// Definitions needed by more than one of the parser's translation units.
// Anything used by only one of them stays file-static in that file.
//
//===----------------------------------------------------------------===//
#pragma once

#include <string>

#include "plang/Basic/StringUtil.h"

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
// A label that is not all digits is not a label at all under ISO 7185/EP;
// Sema is where that is said, so those dialects see it unchanged and are
// reported there.  Turbo Pascal instead allows an ORDINARY IDENTIFIER as a
// label, and identifiers are case-insensitive -- `label Done;` and `goto
// DONE` name the same label.  That has to be settled here too, and not left
// to each downstream consumer to compare case-insensitively on its own:
// unlike the symbol table (which lowercases its own lookup key internally),
// Sema's CurrentBlockLabels/LabelEnclosingStmt and CodeGen's
// LabelGotoEngine::LabelBlocks/declaresLabel/nonLocalTargets all key on this
// string with plain, case-sensitive equality.  Lower-casing it once, here, is
// what keeps every one of those agreeing without having to know that.
inline std::string canonicalLabel(const std::string& Lexeme) {
    for (char C : Lexeme)
        if (!std::isdigit(static_cast<unsigned char>(C))) return toLower(Lexeme);
    const auto First = Lexeme.find_first_not_of('0');
    return First == std::string::npos ? "0" : Lexeme.substr(First);
}

} // namespace plang
