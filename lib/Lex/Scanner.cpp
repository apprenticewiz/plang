#include "plang/Lex/Scanner.h"

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string_view>

using namespace plang;

using KW = std::pair<std::string_view, TokenKind>;

// The reserved words, built from the one list in TokenKinds.def.  Spellings
// there are lowercase, and an identifier is folded before it is looked up,
// which is how Pascal's case-insensitivity is arranged.
static constexpr std::array keywords {
#define KEYWORD(Id, Spelling) KW{Spelling, TokenKind::Id},
#include "plang/Basic/TokenKinds.def"
};

static_assert(std::ranges::is_sorted(keywords, {}, &KW::first),
              "the KEYWORD entries in TokenKinds.def must stay in ascending "
              "order of spelling: this table is binary-searched");

// True for a word only ISO 10206 reserves.  Under -std=iso7185 the scanner
// hands these back as identifiers, since a conforming ISO 7185 program is
// entitled to use one as a name.
static constexpr bool isEPOnlyKeyword(TokenKind K) {
    switch (K) {
#define EPKEYWORD(Id, Spelling) case TokenKind::Id:
#include "plang/Basic/TokenKinds.def"
        return true;
    default:
        return false;
    }
}

Scanner::Scanner(SourceManager& SM, std::string Filename,
                 DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts(Opts), SM(&SM), Diags(Diags), Pos(0) {
    if (auto ID = SM.addFile(Filename)) {
        FID  = *ID;
        Text = SM.getBufferData(FID);
        return;
    }
    // No buffer, so no location to report it at; Text stays empty and next()
    // returns Eof at once.
    emitError({}, diag::err_file_not_found, {Filename});
}

Scanner::Scanner(SourceManager& SM, std::string SourceName, std::string Content,
                 DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts(Opts), SM(&SM), Diags(Diags), Pos(0) {
    FID  = SM.addBuffer(std::move(SourceName), std::move(Content));
    Text = SM.getBufferData(FID);
}

Token Scanner::next() {
    for (;;) {
        skipWhitespaceAndComments();
        if (Pos >= Text.size())
            return make(TokenKind::Eof, "", Pos);

        const size_t TokenStart = Pos;
        const char   C          = Text[Pos];

        Token Tok;
        if (std::isalpha(C) || C == '_')
            Tok = scanIdentifierOrKeyword(TokenStart);
        else if (std::isdigit(C))
            Tok = scanNumber(TokenStart);
        else if (C == '\'')
            Tok = scanString(TokenStart);
        else
            Tok = scanSymbol(TokenStart);

        // Skip Error tokens so the Parser never sees them.
        if (Tok.Kind != TokenKind::Error) return Tok;
    }
}

char Scanner::peek() const {
    return (Pos + 1 < Text.size()) ? Text[Pos + 1] : '\0';
}

void Scanner::skipWhitespaceAndComments() {
    while (Pos < Text.size()) {
        char C = Text[Pos];
        if (std::isspace(static_cast<unsigned char>(C))) {
            ++Pos;
        } else if (C == '{') {
            skipBraceComment();
        } else if (C == '(' && peek() == '*') {
            skipParenthesisComment();
        } else {
            break;
        }
    }
}

// ISO §6.1.8 gives a comment as
//
//     ( '{' | '(*' ) commentary ( '*)' | '}' )
//
// and note 1 spells out what that means: one may open with `{` and close with
// `*)`, or open with `(*` and close with `}`.  The two delimiters are not a
// pair to be matched — either terminator ends either comment — so both are
// looked for here whichever opened it.
void Scanner::skipComment(bool Braced) {
    const size_t CommentStart = Pos;
    if (Braced) ++Pos; else Pos += 2;
    while (Pos < Text.size()) {
        const char C = Text[Pos++];
        if (C == '}') return;
        if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
            ++Pos;
            return;
        }
    }
    emitError(locAt(CommentStart), diag::err_unterminated_comment);
}

void Scanner::skipBraceComment()       { skipComment(/*Braced=*/true); }
void Scanner::skipParenthesisComment() { skipComment(/*Braced=*/false); }

Token Scanner::scanIdentifierOrKeyword(size_t TokenStart) {
    size_t Start = Pos;
    bool Underscore = false;
    while (Pos < Text.size() && (std::isalnum(Text[Pos]) || Text[Pos] == '_')) {
        if (Text[Pos] == '_') Underscore = true;
        ++Pos;
    }
    std::string Word  = std::string(Text.substr(Start, Pos - Start));
    // ISO 7185 §6.1.3 builds an identifier from letters and digits; the
    // underscore is one of the things ISO 10206 §6.1.3 added.  Clause 5.1 h)
    // asks that a use of an extension be reportable, and -std=iso7185 is where
    // that reporting happens, so this cannot pass silently there.
    if (Underscore && !Opts.has(LangOptions::Feature::UnderscoreIdentifiers))
        emitError(locAt(TokenStart), diag::err_ep_underscore_in_identifier);
    // ISO 10206 §6.1.3's grammar -- identifier = letter { [ underscore ]
    // ( letter | digit ) } . -- interleaves each optional underscore with a
    // MANDATORY following letter-or-digit, so a leading, trailing, or
    // doubled underscore can never come out of it; the clause's own NOTE
    // says so directly.  This is an EP-specific restriction on top of
    // underscores being allowed at all (Turbo's C-like identifiers won't
    // have it), so it asks extendedPascal() rather than the shared
    // UnderscoreIdentifiers feature -- see the dialect-vs-feature note in
    // LangOptions.h.
    if (Underscore && Opts.extendedPascal() &&
        (Word.front() == '_' || Word.back() == '_' ||
         Word.find("__") != std::string::npos))
        emitError(locAt(TokenStart), diag::err_ep_underscore_placement);
    std::string Lower = toLower(Word);
    auto It = std::ranges::lower_bound(keywords, Lower, {}, &KW::first);
    TokenKind Kind = (It != keywords.end() && It->first == Lower) ? It->second
                                                                   : TokenKind::Identifier;
    // EP-only keywords are plain identifiers in iso7185 mode.
    if (isEPOnlyKeyword(Kind) && !Opts.extendedPascal())
        Kind = TokenKind::Identifier;
    return make(Kind, Word, TokenStart);
}

Token Scanner::scanNumber(size_t TokenStart) {
    size_t Start = Pos;
    while (Pos < Text.size() && std::isdigit(Text[Pos])) { ++Pos; }

    // EP §6.1.7: nondecimal integer literal  base '#' digits
    if (Opts.extendedPascal() && Pos < Text.size() && Text[Pos] == '#') {
        std::string BaseStr = std::string(Text.substr(Start, Pos - Start));
        int Base = 0;
        for (char C : BaseStr) Base = Base * 10 + (C - '0');
        if (Base < 2 || Base > 36) {
            emitError(locAt(TokenStart),
                      diag::err_nondecimal_base_range);
            return make(TokenKind::Error, BaseStr, TokenStart);
        }
        ++Pos; // consume '#'
        size_t DigStart = Pos;
        while (Pos < Text.size() && std::isalnum(Text[Pos])) { ++Pos; }
        if (Pos == DigStart) {
            emitError(locAt(TokenStart), diag::err_nondecimal_no_digits);
            return make(TokenKind::Error, "#", TokenStart);
        }
        int64_t Value = 0;
        for (size_t I = DigStart; I < Pos; ++I) {
            char C  = Text[I];
            int  D  = std::isdigit(C) ? C - '0'
                                      : std::tolower(C) - 'a' + 10;
            if (D >= Base) {
                std::string cs(1, C);
                std::string bs = std::to_string(Base);
                emitError(locAt(TokenStart),
                          diag::err_nondecimal_bad_digit,
                          {std::string_view(cs), std::string_view(bs)});
                return make(TokenKind::Error,
                            std::string(Text.substr(DigStart, Pos - DigStart)),
                            TokenStart);
            }
            Value = Value * Base + D;
        }
        return make(TokenKind::IntLit, std::to_string(Value), TokenStart);
    }

    // ISO §6.1.5: the fractional part is a digit-sequence, so the point belongs
    // to the number only when a digit follows it.  `1..3` was the case this had
    // to get right, and testing for the second point got that one alone: in
    // `(.1..3.)` the closing bracket is a point too, and taking it left the
    // bracket unclosed.
    bool IsReal = false;
    if (Pos < Text.size() && Text[Pos] == '.'
            && std::isdigit(static_cast<unsigned char>(peek()))) {
        ++Pos;
        while (Pos < Text.size() && std::isdigit(Text[Pos])) { ++Pos; }
        IsReal = true;
    }

    // ISO §6.1.5 scale-factor: 'e' [sign] digits, with or without a fractional
    // part before it, so both 1e3 and 1.5e-2 are real literals.  The exponent
    // is only taken when a digit really follows: otherwise the 'e' belongs to
    // whatever comes next, and swallowing it would turn a neighboring word
    // into a malformed number rather than leaving it to be scanned.
    if (Pos < Text.size() && (Text[Pos] == 'e' || Text[Pos] == 'E')) {
        size_t Look = Pos + 1;
        if (Look < Text.size() && (Text[Look] == '+' || Text[Look] == '-')) ++Look;
        if (Look < Text.size() && std::isdigit(Text[Look])) {
            Pos = Look;
            while (Pos < Text.size() && std::isdigit(Text[Pos])) { ++Pos; }
            IsReal = true;
        }
    }

    return make(IsReal ? TokenKind::RealLit : TokenKind::IntLit,
                std::string(Text.substr(Start, Pos - Start)), TokenStart);
}

Token Scanner::scanString(size_t TokenStart) {
    ++Pos;
    std::string Lexeme;
    while (Pos < Text.size()) {
        if (Text[Pos] == '\n') {
            emitError(locAt(Pos), diag::err_unterminated_string);
            break;
        }
        if (Text[Pos] == '\'') {
            ++Pos;
            if (Pos < Text.size() && Text[Pos] == '\'') {
                ++Pos;
                Lexeme += '\'';
            } else {
                return make(TokenKind::StringLit, Lexeme, TokenStart);
            }
        } else {
            Lexeme += Text[Pos++];
        }
    }
    // Unterminated (hit EOF or newline) — emit error and return the partial content.
    if (Pos >= Text.size())
        emitError(locAt(Pos), diag::err_unterminated_string);
    return make(TokenKind::StringLit, Lexeme, TokenStart);
}

Token Scanner::scanSymbol(size_t TokenStart) {
    char C = Text[Pos++];
    switch (C) {
        case '+': return make(TokenKind::Plus,   "+", TokenStart);
        case '-': return make(TokenKind::Minus,  "-", TokenStart);
        case '*':
            if (Pos < Text.size() && Text[Pos] == '*') { ++Pos; return make(TokenKind::StarStar, "**", TokenStart); }
            return make(TokenKind::Times,  "*", TokenStart);
        case '/': return make(TokenKind::Divide, "/", TokenStart);
        case '=': return make(TokenKind::Equal,  "=", TokenStart);
        case '<':
            if (Pos < Text.size() && Text[Pos] == '>') { ++Pos; return make(TokenKind::NotEqual,           "<>", TokenStart); }
            if (Pos < Text.size() && Text[Pos] == '=') { ++Pos; return make(TokenKind::LessThanOrEqual,    "<=", TokenStart); }
            return make(TokenKind::LessThan, "<", TokenStart);
        case '>':
            if (Pos < Text.size() && Text[Pos] == '=') { ++Pos; return make(TokenKind::GreaterThanOrEqual, ">=", TokenStart); }
            if (Pos < Text.size() && Text[Pos] == '<') { ++Pos; return make(TokenKind::SymDiff, "><", TokenStart); }
            return make(TokenKind::GreaterThan, ">", TokenStart);
        case ':':
            if (Pos < Text.size() && Text[Pos] == '=') { ++Pos; return make(TokenKind::Assign, ":=", TokenStart); }
            return make(TokenKind::Colon, ":", TokenStart);
        // ISO §6.1.9: `(.` and `.)` are the alternative representations of the
        // brackets, and a processor whose character set has the reference
        // characters — as this one's does — provides both, the two spellings
        // being the same token.  They are for terminals that had no brackets,
        // and the Pascal that came from those terminals is still around.
        case '.':
            if (Pos < Text.size() && Text[Pos] == '.') { ++Pos; return make(TokenKind::DotDot, "..", TokenStart); }
            if (Pos < Text.size() && Text[Pos] == ')') { ++Pos; return make(TokenKind::RightBracket, ".)", TokenStart); }
            return make(TokenKind::Dot, ".", TokenStart);
        case ',':  return make(TokenKind::Comma,        ",", TokenStart);
        case ';':  return make(TokenKind::Semicolon,    ";", TokenStart);
        case '(':
            if (Pos < Text.size() && Text[Pos] == '.') { ++Pos; return make(TokenKind::LeftBracket, "(.", TokenStart); }
            return make(TokenKind::LeftParen,    "(", TokenStart);
        case ')':  return make(TokenKind::RightParen,   ")", TokenStart);
        case '[':  return make(TokenKind::LeftBracket,  "[", TokenStart);
        case ']':  return make(TokenKind::RightBracket, "]", TokenStart);
        // ISO §6.1.9 gives '@' as the alternative for '^', and the two "shall
        // not be distinguished".  Providing it is implementation-defined, and
        // it is provided: the Pascal written for terminals lacking the arrow
        // spells every pointer type and every dereference this way, and a
        // program using it is standard Pascal.
        case '^':  return make(TokenKind::Caret,        "^", TokenStart);
        case '@':  return make(TokenKind::Caret,        "@", TokenStart);
        default:
            std::string cs(1, C);
            emitError(locAt(TokenStart),
                      diag::err_unexpected_char, {std::string_view(cs)});
            return make(TokenKind::Error, std::string(1, C), TokenStart);
    }
}

void Scanner::emitError(SourceLocation Loc, std::string Msg) {
    Diags.report(Loc, DiagSeverity::Error, std::move(Msg));
}

void Scanner::emitError(SourceLocation Loc, DiagID ID,
                        std::initializer_list<std::string_view> Args) {
    Diags.report(Loc, ID, Args);
}

Token Scanner::make(TokenKind Kind, std::string Lexeme, size_t TokenStart) {
    return Token{Kind, std::move(Lexeme), locAt(TokenStart)};
}
