/// plang-po.cpp — writes the .po catalogs from the diagnostic .def files
///
/// Run at build time.  The English a translator works from is not maintained
/// by hand: it is the text in the four DiagnosticKinds .def files, and this
/// writes it out in the format translators' tools expect.  One source, so the
/// two cannot drift.
///
/// WHY THIS IS A C++ TOOL AND NOT A SCRIPT
///
/// 53 of the 193 messages are written as several adjacent string literals that
/// the C preprocessor concatenates:
///
///     DIAG(err_ep_extension, Error,
///          "%0 is an Extended Pascal extension and is not available under "
///          "-std=iso7185")
///
/// A line-based extractor truncates every one of them.  Including the .def
/// here means the preprocessor does the concatenation, exactly as it does for
/// the compiler itself, so there is no second reader of the .def grammar that
/// could disagree with the first.
///
/// The one thing the preprocessor cannot see is comments, and the ISO
/// citations written above many entries are the best translator context there
/// is.  So there is a second, deliberately dumb pass: it looks only for the
/// line starting `DIAG(<id>` and walks back over the `//` lines above it.  It
/// never has to read a message, so the concatenation that defeats a naive
/// extractor never reaches it.
///
/// The generated files are build artefacts.  They are not checked in, because
/// a checked-in copy is a second source of truth and the drift it invites is
/// the thing this exists to prevent.

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/Token.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace plang;

namespace {

/// One message, as the preprocessor sees it.
struct Entry {
    std::string Id;
    std::string English;
};

std::vector<Entry> allEntries() {
    std::vector<Entry> V;
#define DIAG(ID, LEVEL, MSG) V.push_back({#ID, MSG});
#include "plang/Basic/DiagnosticMessages.def"
#undef DIAG
    return V;
}

/// A .po string body: the format's escapes are C's, and the reader accepts
/// only these four, so only these four are ever written.
std::string escape(std::string_view S) {
    std::string Out;
    Out.reserve(S.size() + 8);
    for (const char C : S) {
        switch (C) {
        case '\\': Out += "\\\\"; break;
        case '"':  Out += "\\\""; break;
        case '\n': Out += "\\n";  break;
        case '\t': Out += "\\t";  break;
        default:   Out += C;      break;
        }
    }
    return Out;
}

/// The `//` comment block written immediately above each `DIAG(` line, keyed
/// by diagnostic identifier.  This is what a translator reads to find out what
/// a message is about, so it is worth carrying even though the preprocessor
/// throws it away.
void collectComments(const std::string& Path,
                     std::map<std::string, std::string>& Out) {
    std::ifstream In(Path);
    if (!In) {
        std::cerr << "plang-po: cannot read " << Path << "\n";
        return;
    }
    std::vector<std::string> Lines;
    for (std::string L; std::getline(In, L);) Lines.push_back(L);

    for (std::size_t I = 0; I < Lines.size(); ++I) {
        const std::string& L = Lines[I];
        if (L.rfind("DIAG(", 0) != 0) continue;
        const std::size_t Comma = L.find(',', 5);
        if (Comma == std::string::npos) continue;
        const std::string Id = L.substr(5, Comma - 5);

        // Walk back over the contiguous comment block.  A blank line or any
        // other statement ends it, so a comment about the file rather than
        // about this entry is not picked up.
        std::vector<std::string> Block;
        for (std::size_t J = I; J-- > 0;) {
            std::string_view T = Lines[J];
            while (!T.empty() && (T.front() == ' ' || T.front() == '\t'))
                T.remove_prefix(1);
            if (T.rfind("//", 0) != 0) break;
            T.remove_prefix(2);
            if (!T.empty() && T.front() == ' ') T.remove_prefix(1);
            Block.emplace_back(T);
        }
        if (Block.empty()) continue;
        std::string Text;
        for (std::size_t K = Block.size(); K-- > 0;) {
            Text += Block[K];
            if (K) Text += '\n';
        }
        Out[Id] = Text;
    }
}

void usage() {
    std::cerr <<
        "usage: plang-po --output <file> [--pseudo] [--language <tag>]\n"
        "                [--def <DiagnosticKinds.def>]...\n"
        "\n"
        "Writes the diagnostic message catalog in GNU gettext .po format.\n"
        "  --pseudo    every message wrapped in [! !], for proving that a\n"
        "              build really loaded a catalog rather than falling back\n";
}

} // namespace

int main(int Argc, char** Argv) {
    std::string Output, Language = "en_US";
    bool Pseudo = false;
    std::vector<std::string> Defs;

    for (int I = 1; I < Argc; ++I) {
        const std::string A = Argv[I];
        if (A == "--output"   && I + 1 < Argc) Output   = Argv[++I];
        else if (A == "--language" && I + 1 < Argc) Language = Argv[++I];
        else if (A == "--def" && I + 1 < Argc) Defs.emplace_back(Argv[++I]);
        else if (A == "--pseudo") Pseudo = true;
        else { usage(); return 1; }
    }
    if (Output.empty()) { usage(); return 1; }

    std::map<std::string, std::string> Comments;
    for (const auto& D : Defs) collectComments(D, Comments);

    std::ostringstream O;
    O << "# plang diagnostic messages.\n"
         "#\n"
         "# GENERATED from the DiagnosticKinds .def files by plang-po.  The\n"
         "# English here is written in those files; edit them, not this.  A\n"
         "# translation is a copy of this file with each msgstr filled in.\n"
         "#\n"
         "# An entry is found by its msgctxt, which holds the diagnostic's\n"
         "# identifier.  The msgid is the English, for reference; changing it\n"
         "# does not change what the entry means.\n"
         "#\n"
         "# %0..%9 are the arguments plang substitutes.  A translation may put\n"
         "# them in whatever order its language needs, but must use the same\n"
         "# set: one that drops or invents a placeholder is refused and the\n"
         "# English is used instead.\n"
         "#\n"
         "# An entry left with an empty msgstr, or marked \"#, fuzzy\", is not\n"
         "# used -- plang falls back to English for that one message alone, so\n"
         "# a translation is useful long before it is finished.\n"
         "#\n";
    if (Pseudo)
        O << "# THIS IS NOT A TRANSLATION.  Every message is the English\n"
             "# wrapped in [! !], so that a test can tell a build that loaded\n"
             "# a catalog from one that silently fell back to English.  The\n"
             "# fallback is deliberate and total, which is exactly what makes\n"
             "# a broken install otherwise invisible.\n"
             "#\n";
    O << "msgid \"\"\n"
         "msgstr \"\"\n"
         "\"Project-Id-Version: plang\\n\"\n"
         "\"Language: " << Language << "\\n\"\n"
         "\"MIME-Version: 1.0\\n\"\n"
         "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
         "\"Content-Transfer-Encoding: 8bit\\n\"\n"
         "\"X-Plang-Catalog-ABI: 1\\n\"\n";

    // The token descriptions and the severity labels.  They are not messages,
    // but they are English printed inside one -- "expected identifier, got end
    // of file", and the "error:" in front of every line -- so a catalog that
    // left them out would translate most of a diagnostic and not all of it.
    O << "\n#. A kind of token, named in \"expected %0, got %1\".  Tokens with a\n"
         "#. fixed spelling -- ';', 'begin' -- are Pascal syntax and are not here.\n";
// PUNCT and KEYWORD must be silenced explicitly.  TokenKinds.def defines them
// in terms of TOK when a consumer has not, so defining TOK alone would offer a
// translator every operator and every reserved word -- ';', 'begin', 'array'.
// Those are Pascal syntax: describe() never asks the catalog about them, and
// putting them in front of a translator invites a change that could not have
// any effect and might be attempted anyway.
#define TOK(Id, Description)                                                   \
    O << "\n" << "msgctxt \"token/" #Id "\"\n"                                 \
      << "msgid \"" << escape(Description) << "\"\n"                           \
      << "msgstr \"" << (Pseudo ? "[!" + escape(Description) + "!]" : "") << "\"\n";
#define PUNCT(Id, Spelling)
#define KEYWORD(Id, Spelling)
#define DIALECT_KEYWORD(Id, Spelling, Dialects)
#include "plang/Basic/TokenKinds.def"
#undef DIALECT_KEYWORD
#undef KEYWORD
#undef PUNCT
#undef TOK

    O << "\n#. The word printed in front of a diagnostic.\n";
    for (const char* L : {"error", "warning", "note"})
        O << "\nmsgctxt \"label/" << L << "\"\n"
          << "msgid \"" << L << "\"\n"
          << "msgstr \"" << (Pseudo ? std::string("[!") + L + "!]" : "") << "\"\n";

    for (const auto& E : allEntries()) {
        O << "\n";
        if (const auto It = Comments.find(E.Id); It != Comments.end()) {
            std::istringstream Cs(It->second);
            for (std::string L; std::getline(Cs, L);) O << "#. " << L << "\n";
        }
        O << "msgctxt \"diag/" << E.Id << "\"\n"
          << "msgid \""  << escape(E.English) << "\"\n"
          << "msgstr \"";
        if (Pseudo) O << "[!" << escape(E.English) << "!]";
        O << "\"\n";
    }

    std::ofstream Out(Output, std::ios::binary);
    if (!Out) {
        std::cerr << "plang-po: cannot write " << Output << "\n";
        return 1;
    }
    Out << O.str();
    if (!Out) {
        std::cerr << "plang-po: failed writing " << Output << "\n";
        return 1;
    }
    return 0;
}
