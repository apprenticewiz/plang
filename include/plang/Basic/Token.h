#pragma once

#include "plang/Basic/SourceLocation.h"

#include <format>
#include <string>
#include <string_view>

namespace plang {

    /// The category of a lexical token.
    ///
    /// Generated from TokenKinds.def, which is the only place the set of tokens
    /// is written down.  Do not add an enumerator here; add a line there.
    enum class TokenKind {
#define TOK(Id, Description) Id,
#include "plang/Basic/TokenKinds.def"
    };

    /// Return the printable name of a TokenKind enumerator, e.g. "Plus".
    ///
    /// This is the name of the enumerator and not the token's source spelling,
    /// so it belongs in an AST dump or an internal-error message rather than in
    /// a diagnostic aimed at someone reading their own program.  For that, use
    /// describe().
    [[nodiscard]] constexpr std::string_view kindName(TokenKind K) {
        switch (K) {
#define TOK(Id, Description) case TokenKind::Id: return #Id;
#include "plang/Basic/TokenKinds.def"
        }
        return "Unknown";
    }

    /// Return true if every token of kind K is spelled the same way in source.
    ///
    /// True for keywords, operators and delimiters; false for the tokens whose
    /// text varies, such as an identifier or a literal.
    [[nodiscard]] constexpr bool hasFixedSpelling(TokenKind K) {
        switch (K) {
#define PUNCT(Id, Spelling)   case TokenKind::Id:
#define KEYWORD(Id, Spelling) case TokenKind::Id:
#include "plang/Basic/TokenKinds.def"
            return true;
        default:
            return false;
        }
    }

    /// Return how a token of kind K is written in source: "+", ";", "div".
    ///
    /// For a kind with no fixed spelling this gives the description instead —
    /// "identifier", "integer literal" — since there is nothing else to say.
    /// Use hasFixedSpelling to tell the two apart, or describe() to get a
    /// string already punctuated for a diagnostic.
    [[nodiscard]] constexpr std::string_view spelling(TokenKind K) {
        switch (K) {
#define TOK(Id, Description) case TokenKind::Id: return Description;
#define PUNCT(Id, Spelling)  case TokenKind::Id: return Spelling;
#define KEYWORD(Id, Spelling) case TokenKind::Id: return Spelling;
#include "plang/Basic/TokenKinds.def"
        }
        return "unknown token";
    }

    /// Return the source spelling of an operator, for use in a diagnostic.
    ///
    /// Kept as a name of its own because that is what the callers mean; it is
    /// spelling() under another name.
    [[nodiscard]] constexpr std::string_view opSpelling(TokenKind K) {
        return spelling(K);
    }

    /// Name a token kind the way a diagnostic should: a fixed spelling in
    /// quotes, and anything else described in words.  Gives "';'" for a
    /// semicolon and "identifier" for an identifier, so that "expected %0"
    /// reads correctly either way.
    [[nodiscard]] inline std::string describe(TokenKind K) {
        if (!hasFixedSpelling(K)) return std::string(spelling(K));
        return "'" + std::string(spelling(K)) + "'";
    }

    /// A single lexical token produced by the Scanner.
    struct Token {
        /// Category of this token.
        TokenKind      Kind{TokenKind::Eof};
        /// Exact source text; empty for Eof.
        std::string    Lexeme;
        /// Where the token's first character is.  Ask a SourceManager to turn
        /// this into a filename, a line and a column.
        SourceLocation Loc;

        /// A token converts to where it is.
        ///
        /// This is what lets error(Tok, ...) and node->Loc = Tok read the way
        /// they always have, now that a node stores a position rather than a
        /// whole token.  The conversion loses nothing: a location is all the
        /// position a token has.
        constexpr operator SourceLocation() const { return Loc; }

        /// Where the token is.  Spelled out for the places that would rather
        /// say so than lean on the conversion.
        [[nodiscard]] constexpr SourceLocation toLoc() const { return Loc; }

        /// Format this token as a human-readable string for debugging.
        /// The position is left out: rendering it needs a SourceManager, and
        /// this exists for a quick look at what was scanned.
        [[nodiscard]] std::string toString() const {
            return std::format("Token {{ kind={}, lexeme=\"{}\" }}",
                               kindName(Kind), Lexeme);
        }
    };

} // namespace plang
