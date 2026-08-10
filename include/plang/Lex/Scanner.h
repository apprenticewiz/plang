#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Basic/Token.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Lexical analyser for Pascal source files.
/// Reads the entire file into memory on construction, then yields one token at
/// a time via next().  Repeated calls after Eof safely return Eof again.
///
/// All lexical errors are appended to the shared diagnostics vector passed at
/// construction; the Scanner never throws.  On an unrecognised character it
/// emits a diagnostic and returns TokenKind::Error (the Parser skips these).
/// On an unterminated comment or string it emits a diagnostic and resumes
/// scanning from the nearest sensible point.
class Scanner {
public:
    /// Reads Filename into \p SM and scans it.
    /// On failure, reports a diagnostic and leaves the scanner in an empty
    /// state (next() will immediately return Eof).
    explicit Scanner(SourceManager& SM, std::string Filename,
                     DiagnosticsEngine& Diags, LangOptions Opts = {});

    /// Scan an in-memory buffer instead of a file.
    /// \p SourceName is what diagnostics call it (e.g. "<pmi>").
    explicit Scanner(SourceManager& SM, std::string SourceName,
                     std::string Content, DiagnosticsEngine& Diags,
                     LangOptions Opts = {});

    /// Returns the next token from the source, advancing past it.
    /// Skips whitespace, comments, and TokenKind::Error tokens before returning.
    /// Returns TokenKind::Eof once the end of input is reached and on every
    /// subsequent call.
    Token next();

private:
    LangOptions          Opts;   // dialect and warning options (owned copy)
    const SourceManager* SM;     // owns the text; not owned here
    FileID               FID;    // the buffer being scanned
    std::string_view     Text;   // that buffer's text, owned by SM
    DiagnosticsEngine&   Diags;  // shared diagnostic sink
    size_t               Pos;    // index of the next character to be consumed

    // The position of byte Offset in the buffer.  Line and column are the
    // SourceManager's business, which is why the scanner tracks neither.
    [[nodiscard]] SourceLocation locAt(size_t Offset) const {
        return SM->getLocForOffset(FID, Offset);
    }

    // Returns the character at Pos+1 without advancing, or '\0' at end of input.
    char peek() const;

    // Advances past whitespace and Pascal comments ({ } or (* *)).
    void skipWhitespaceAndComments();

    // Advances past a comment, opened with '{' when \p Braced and with '(*'
    // otherwise.  ISO §6.1.8 lets either terminator close either, so which one
    // opened it decides only how many characters to step over.  An
    // unterminated comment appends a diagnostic and returns where it stopped.
    void skipComment(bool Braced);
    void skipBraceComment();
    void skipParenthesisComment();

    Token scanIdentifierOrKeyword(size_t TokenStart);
    Token scanNumber(size_t TokenStart);

    // Scans a single-quoted string.  On an unterminated or newline-spanning
    // string, appends a diagnostic and returns the partial content as a StringLit.
    Token scanString(size_t TokenStart);

    // Scans a one- or two-character operator / delimiter.
    // Returns TokenKind::Error (with the bad character as the lexeme) for any
    // character not recognised as a valid Pascal symbol.
    Token scanSymbol(size_t TokenStart);

    void emitError(SourceLocation Loc, std::string Msg);
    void emitError(SourceLocation Loc, DiagID ID,
                   std::initializer_list<std::string_view> Args = {});

    Token make(TokenKind Kind, std::string Lexeme, size_t TokenStart);
};

} // namespace plang
