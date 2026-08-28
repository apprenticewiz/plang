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

// MS-DOS text files end with a Ctrl-Z (0x1A) byte, and Turbo Pascal's scanner
// -- reading through the DOS text-mode CRT, not raw bytes -- never saw
// anything written after one.  -std=turbo reproduces that at the buffer
// level, once, before any scanning starts: everything from the first 0x1A
// onward is simply not there, the same way it never reached TP's own lexer.
// ISO 7185 and Extended Pascal have no such convention and are left alone;
// 0x1A is just another byte to them (an unexpected character wherever it's
// not inside a string or comment, same as any other control byte).
//
// A static member (declared in Scanner.h) rather than a file-local free
// function, so that Directives.cpp's openInclude can apply the identical
// rule to a buffer an {$I file}/{$INCLUDE file} opens mid-scan, the same
// way each constructor below already applies it to the buffer it opens.
std::string_view Scanner::truncateAtCtrlZ(std::string_view Text, const LangOptions& Opts) {
    if (!Opts.turbo()) return Text;
    if (const size_t Z = Text.find('\x1A'); Z != std::string_view::npos)
        return Text.substr(0, Z);
    return Text;
}

using KW = std::pair<std::string_view, TokenKind>;

// The reserved words, built from the one list in TokenKinds.def.  Spellings
// there are lowercase, and an identifier is folded before it is looked up,
// which is how Pascal's case-insensitivity is arranged.
static constexpr std::array keywords {
#define KEYWORD(Id, Spelling) KW{Spelling, TokenKind::Id},
#include "plang/Basic/TokenKinds.def"
};

static_assert(std::ranges::is_sorted(keywords, {}, &KW::first),
              "the KEYWORD and DIALECT_KEYWORD entries in TokenKinds.def must "
              "stay in ascending order of spelling: this table is "
              "binary-searched");

// The dialects that reserve K, as D_* bits (LangOptions::DialectBits).  A
// word not built from DIALECT_KEYWORD -- an ordinary KEYWORD -- is reserved
// in every dialect, so the switch has no case for it and the default answers
// "all of them."  Under a dialect not in this set the scanner hands the word
// back as an identifier, since a conforming program in that dialect is
// entitled to use it as a name.
static constexpr unsigned keywordDialects(TokenKind K) {
    switch (K) {
#define DIALECT_KEYWORD(Id, Spelling, Dialects) case TokenKind::Id: return (Dialects);
#include "plang/Basic/TokenKinds.def"
    default:
        return ~0u;
    }
}

// True for a token kind that can only ever begin an expression, never a
// type-denoter.  This is next()'s allow-list for when a fresh '^' may start
// a Turbo `^ctrl` control-character literal instead of the existing Caret
// token (PrevKind's comment in Scanner.h has the full rationale).
//
// Deliberately an allow-list rather than trying to block-list the tokens
// that DO introduce a type -- Colon (var/field/parameter/result types),
// Equal (only inside a `type X = ...` declaration, but indistinguishable
// from `const X = ...`'s Equal by token kind alone) and Of (`array of T` /
// `set of T` / `file of T`, but indistinguishable from a case-label's Of by
// token kind alone) are each ambiguous with a legitimate expression-start
// use of the very same token, so getting a block-list exactly right would
// mean tracking which declaration section is currently open.  Restricting to
// tokens that are NEVER followed by a type-denoter sidesteps that entirely,
// at the cost of not recognizing `^X` immediately after `:`, `=`, or `of`
// (e.g. `const CR = ^M;`, `case c of ^M: ...`) -- `#code` is unconditional
// (see next()) and is the way to spell a control character in exactly those
// positions instead.
static constexpr bool startsExpression(TokenKind K) {
    switch (K) {
    case TokenKind::Assign:
    case TokenKind::LeftParen:
    case TokenKind::LeftBracket:
    case TokenKind::Comma:
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Times:
    case TokenKind::Divide:
    case TokenKind::StarStar:
    case TokenKind::LessThan:
    case TokenKind::GreaterThan:
    case TokenKind::LessThanOrEqual:
    case TokenKind::GreaterThanOrEqual:
    case TokenKind::NotEqual:
    case TokenKind::SymDiff:
    case TokenKind::And:
    case TokenKind::Or:
    case TokenKind::Not:
    case TokenKind::Div:
    case TokenKind::Mod:
    case TokenKind::In:
        return true;
    default:
        return false;
    }
}

Scanner::Scanner(SourceManager& SM, std::string Filename,
                 DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts(Opts), SM(&SM), Diags(Diags), Pos(0) {
    // Seeded here rather than left as a reference to Opts.Defines: `{$DEFINE}`/
    // `{$UNDEF}` mutate this copy in place as they are scanned (see
    // CurrentDefines's own comment in Scanner.h), and Opts.Defines is the
    // read-only starting point, not something this scanner may change out
    // from under whoever else is holding the same LangOptions.  Folded
    // through toLower again regardless of whether Opts.Defines already was,
    // since nothing enforces that on every caller that builds one.
    for (const std::string& S : this->Opts.Defines) CurrentDefines.insert(toLower(S));
    if (auto ID = SM.addFile(Filename)) {
        FID  = *ID;
        Text = truncateAtCtrlZ(SM.getBufferData(FID), Opts);
        // This file's own identity, so that a self-including {$I <this
        // file's own name>} is caught by openInclude's ordinary
        // OpenIncludePaths check rather than recursing at all -- see
        // OpenIncludePaths's own comment in Scanner.h for the invariant
        // this establishes (one entry per file currently open, main file
        // included).
        OpenIncludePaths.push_back(canonicalIdentity(Filename));
        return;
    }
    // No buffer, so no location to report it at; Text stays empty and next()
    // returns Eof at once.  addFile fails both when the file cannot be
    // opened and when SourceManager's coordinate space has no room left for
    // it (see wouldOverflow); a redundant, harmless re-open here (only ever
    // reached on this already-rare path) is what tells the two apart, since
    // addFile itself has no diagnostics engine to report through.
    if (std::ifstream(Filename))
        emitError({}, diag::err_source_too_large, {Filename});
    else
        emitError({}, diag::err_file_not_found, {Filename});
}

Scanner::Scanner(SourceManager& SM, std::string SourceName, std::string Content,
                 DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts(Opts), SM(&SM), Diags(Diags), Pos(0) {
    // See the other constructor's identical loop for why this seeds a
    // mutable copy rather than reading Opts.Defines directly.
    for (const std::string& S : this->Opts.Defines) CurrentDefines.insert(toLower(S));
    // SourceName is not moved from here (unlike Content): the failure path
    // below still needs it, and a moved-from string is only left empty by
    // convention, not by guarantee.
    //
    // Unlike the file-path constructor above, nothing is pushed onto
    // OpenIncludePaths here: SourceName (e.g. "<pmi>") names no real file
    // on disk for a self-include to reopen in the first place, so there is
    // no cycle to protect against for this buffer itself. An {$I file}
    // reached while scanning it (today only reachable if some future
    // caller ever constructs this way under -std=turbo; loadPMI's own use
    // of this constructor always forces -std=iso10206, which has no
    // directives at all) still resolves and is still tracked correctly
    // from that point on -- only the synthetic outermost buffer itself is
    // unprotected.
    if (auto ID = SM.addBuffer(SourceName, std::move(Content))) {
        FID  = *ID;
        Text = truncateAtCtrlZ(SM.getBufferData(FID), Opts);
        return;
    }
    emitError({}, diag::err_source_too_large, {SourceName});
}

Token Scanner::next() {
    for (;;) {
        skipWhitespaceAndComments();
        if (Pos >= Text.size()) {
            // The buffer currently being read has run out, but an
            // {$I file}/{$INCLUDE file} may still have an outer file
            // waiting to resume -- exactly the "splice the included text
            // in, then resume where the directive ended" contract
            // openInclude/popInclude (Directives.cpp) implement.  Checked
            // before the real-Eof handling below: reaching the end of an
            // included buffer is not the end of the token stream unless
            // there is nothing left to pop back to.
            if (popInclude()) continue;
            // A live {$IFDEF}/{$IFNDEF} whose own {$ENDIF} the file simply
            // never reached -- the one unterminated-conditional case
            // skipToNextConditionalMarker cannot itself catch, since it is
            // never called for a branch that stayed live all the way to
            // here.  A no-op (CondStack is always empty) for ISO 7185 and
            // Extended Pascal, and for every subsequent next() call once
            // this has already run once.
            reportUnterminatedConditionals();
            return make(TokenKind::Eof, "", Pos);
        }

        const size_t TokenStart = Pos;
        const char   C          = Text[Pos];

        Token Tok;
        if (std::isalpha(static_cast<unsigned char>(C)) || C == '_')
            Tok = scanIdentifierOrKeyword(TokenStart);
        else if (std::isdigit(static_cast<unsigned char>(C)))
            Tok = scanNumber(TokenStart);
        else if (C == '\'')
            Tok = scanString(TokenStart);
        // Turbo `#code` control-character literal: '#' claims no existing
        // grammar in any dialect (EP's own '#' -- 16#FF -- only ever appears
        // *after* scanNumber has already consumed a leading digit run, a
        // disjoint code path), so this needs no further disambiguation.
        else if (Opts.turbo() && C == '#')
            Tok = scanString(TokenStart);
        // Turbo `$FF` hexadecimal integer literal: likewise unclaimed by any
        // dialect ('$' has no dispatch arm at all outside -std=turbo).
        else if (Opts.turbo() && C == '$')
            Tok = scanHexLiteral(TokenStart);
        // Turbo `^ctrl` control-character literal.  Unlike '#' and '$', '^'
        // is already Caret, so this is only the start of a new literal when
        // it has the right shape (see caretLooksLikeControlChar()) AND the
        // token just before it is one that can only begin an expression --
        // see PrevKind's comment in Scanner.h for why that second condition
        // is what keeps `type PM = ^Integer` a pointer type. Anywhere this
        // doesn't hold, '^' falls through to scanSymbol exactly as before.
        else if (Opts.turbo() && caretLooksLikeControlChar() && startsExpression(PrevKind))
            Tok = scanString(TokenStart);
        else
            Tok = scanSymbol(TokenStart);

        // Skip Error tokens so the Parser never sees them.
        if (Tok.Kind != TokenKind::Error) {
            PrevKind = Tok.Kind;
            return Tok;
        }
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
        // A '{' or '(*' immediately followed by '$' -- no gap -- is a
        // Turbo compiler directive, not an ordinary comment: ISO 7185 and
        // Extended Pascal have no such convention (Opts.turbo() gates this
        // entirely), so `{$anything}` stays a plain, ignored comment there,
        // handled below exactly as it always has been.
        } else if (C == '{') {
            if (Opts.turbo() && peek() == '$') skipDirective(/*Braced=*/true);
            else skipBraceComment();
        } else if (C == '(' && peek() == '*') {
            if (Opts.turbo() && Pos + 2 < Text.size() && Text[Pos + 2] == '$')
                skipDirective(/*Braced=*/false);
            else
                skipParenthesisComment();
        // C++-style line comments are Turbo's own addition (not in ISO 7185
        // or Extended Pascal); a single '/' is division under every dialect
        // including Turbo, so only a doubled '//' qualifies.
        } else if (Opts.turbo() && C == '/' && peek() == '/') {
            skipLineComment();
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
    if (Opts.turbo()) { skipCommentTurbo(Braced); return; }
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

// Borland's rule is the opposite of ISO §6.1.8's: a comment opened with '{'
// is closed only by '}', and one opened with '(*' only by '*)' -- confirmed
// against a real compiler (`fpc -Mtp`) before writing this, not assumed from
// the name "Turbo Pascal" alone.  `fpc -Mtp` on `{ ... *) ... }` compiles
// clean (the embedded, wrong-kind closer is inert -- just comment text) and
// on `{ ... *)` with no real '}' anywhere reports an unterminated comment at
// EOF.  This reproduces both: SawOtherCloser only ever turns an
// EOF-with-no-real-terminator into the more specific
// err_comment_delim_mismatch instead of the generic err_unterminated_comment
// -- it never makes the wrong-kind sequence act as a terminator, so a
// harmless one embedded in an otherwise-well-formed comment still compiles.
void Scanner::skipCommentTurbo(bool Braced) {
    const size_t CommentStart = Pos;
    if (Braced) ++Pos; else Pos += 2;
    bool SawOtherCloser = false;
    while (Pos < Text.size()) {
        const char C = Text[Pos++];
        if (Braced) {
            if (C == '}') return;
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                SawOtherCloser = true;
                ++Pos;
            }
        } else {
            if (C == '*' && Pos < Text.size() && Text[Pos] == ')') {
                ++Pos;
                return;
            }
            if (C == '}') SawOtherCloser = true;
        }
    }
    if (SawOtherCloser) {
        const std::string_view Opener = Braced ? "{" : "(*";
        const std::string_view Closer = Braced ? "}" : "*)";
        emitError(locAt(CommentStart), diag::err_comment_delim_mismatch,
                  {Opener, Closer});
    } else {
        emitError(locAt(CommentStart), diag::err_unterminated_comment);
    }
}

void Scanner::skipBraceComment()       { skipComment(/*Braced=*/true); }
void Scanner::skipParenthesisComment() { skipComment(/*Braced=*/false); }

// A -std=turbo `//` line comment: Pos is at the first '/' on entry (the
// second is confirmed but not yet consumed by the caller); runs to the
// newline that ends it, or EOF, whichever is first.  The newline itself is
// left for skipWhitespaceAndComments' own isspace branch to consume next
// time around, the same division of labor skipComment leaves to it for the
// space after a brace comment.
void Scanner::skipLineComment() {
    Pos += 2; // the two '/'s
    while (Pos < Text.size() && Text[Pos] != '\n') ++Pos;
}

Token Scanner::scanIdentifierOrKeyword(size_t TokenStart) {
    size_t Start = Pos;
    bool Underscore = false;
    while (Pos < Text.size() && (std::isalnum(static_cast<unsigned char>(Text[Pos])) || Text[Pos] == '_')) {
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
    // A keyword the active dialect does not reserve is a plain identifier.
    if ((keywordDialects(Kind) & Opts.dialectBit()) == 0)
        Kind = TokenKind::Identifier;
    return make(Kind, Word, TokenStart);
}

Token Scanner::scanNumber(size_t TokenStart) {
    size_t Start = Pos;
    while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) { ++Pos; }

    // EP §6.1.7: nondecimal integer literal  base '#' digits
    if (Opts.extendedPascal() && Pos < Text.size() && Text[Pos] == '#') {
        std::string BaseStr = std::string(Text.substr(Start, Pos - Start));
        // Checked before the multiply, exactly like the digit-value loop
        // below: a plain `int` accumulator here let a base string like
        // `4294967312` wrap mod 2^32 down to 16 and sail past the range
        // check as valid hex (issue #213). Widening to int64_t alone isn't
        // enough either -- a long enough digit string (e.g. 20 nines)
        // overflows a 64-bit accumulator too -- so overflow is detected
        // incrementally and folded into the same range-error diagnostic,
        // which is correct: any base whose digits overflow is certainly
        // not in [2, 36].
        int64_t Base = 0;
        bool BaseOverflowed = false;
        for (char C : BaseStr) {
            int D = C - '0';
            if (Base > (INT64_MAX - D) / 10) {
                BaseOverflowed = true;
                break;
            }
            Base = Base * 10 + D;
        }
        if (BaseOverflowed || Base < 2 || Base > 36) {
            emitError(locAt(TokenStart),
                      diag::err_nondecimal_base_range);
            return make(TokenKind::Error, BaseStr, TokenStart);
        }
        ++Pos; // consume '#'
        size_t DigStart = Pos;
        while (Pos < Text.size() && std::isalnum(static_cast<unsigned char>(Text[Pos]))) { ++Pos; }
        if (Pos == DigStart) {
            emitError(locAt(TokenStart), diag::err_nondecimal_no_digits);
            return make(TokenKind::Error, "#", TokenStart);
        }
        int64_t Value = 0;
        for (size_t I = DigStart; I < Pos; ++I) {
            char C  = Text[I];
            int  D  = std::isdigit(static_cast<unsigned char>(C)) ? C - '0'
                                      : std::tolower(static_cast<unsigned char>(C)) - 'a' + 10;
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
            // Checked before the multiply, not after: once Value has
            // wrapped there is no way to recover the true magnitude from
            // it, so by the time an after-the-fact check saw a suspicious
            // value the real overflow would already be unrecoverable, the
            // same reason the bad-digit check above rejects its digit
            // before consuming it rather than after.  Base is already
            // known to be in [2, 36] here, so the division can't be by
            // zero.
            if (Value > (INT64_MAX - D) / Base) {
                emitError(locAt(TokenStart), diag::err_nondecimal_literal_out_of_range);
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
        while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) { ++Pos; }
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
        if (Look < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Look]))) {
            Pos = Look;
            while (Pos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[Pos]))) { ++Pos; }
            IsReal = true;
        }
    }

    return make(IsReal ? TokenKind::RealLit : TokenKind::IntLit,
                std::string(Text.substr(Start, Pos - Start)), TokenStart);
}

Token Scanner::scanString(size_t TokenStart) {
    std::string Lexeme;
    for (;;) {
        if (Pos < Text.size() && Text[Pos] == '\'') {
            if (!scanQuotedFragment(Lexeme))
                return make(TokenKind::StringLit, Lexeme, TokenStart);
        } else if (Opts.turbo() && Pos < Text.size() && Text[Pos] == '#') {
            if (!scanControlCodeFragment(Lexeme))
                return make(TokenKind::Error, Lexeme, TokenStart);
        } else if (Opts.turbo() && caretLooksLikeControlChar()) {
            scanCaretFragment(Lexeme);
        } else {
            // Only reachable if scanString is ever entered on something
            // other than the three fragment starts next() already checked;
            // defensive, not a real path today.
            break;
        }
        // Turbo only: glue straight into the next fragment when it starts
        // with no gap at all -- 'AB'#13#10'CD' is one 6-character StringLit.
        // Once a literal is already under way there is no pointer-type
        // ambiguity left to worry about (a type-denoter can never follow a
        // string/code/control fragment), so unlike next()'s fresh-token
        // dispatch this needs no startsExpression(PrevKind) check.
        if (!Opts.turbo() || Pos >= Text.size())
            break;
        if (Text[Pos] != '\'' && Text[Pos] != '#' && !caretLooksLikeControlChar())
            break;
    }
    return make(TokenKind::StringLit, Lexeme, TokenStart);
}

// Scans one '...'-delimited fragment (the doubled '' escape included),
// appending its decoded content to Lexeme.  This is exactly the body the
// single-fragment scanString used to have; factored out so a glued run can
// call it once per quoted piece.  Returns false, having already emitted
// err_unterminated_string, if the quote never closes (a bare newline or
// EOF) -- the caller stops gluing immediately in that case, same as before.
bool Scanner::scanQuotedFragment(std::string& Lexeme) {
    ++Pos; // opening quote
    while (Pos < Text.size()) {
        if (Text[Pos] == '\n') {
            emitError(locAt(Pos), diag::err_unterminated_string);
            return false;
        }
        if (Text[Pos] == '\'') {
            ++Pos;
            if (Pos < Text.size() && Text[Pos] == '\'') {
                ++Pos;
                Lexeme += '\'';
            } else {
                return true;
            }
        } else {
            Lexeme += Text[Pos++];
        }
    }
    emitError(locAt(Pos), diag::err_unterminated_string);
    return false;
}

// Scans one Turbo `#code` fragment: '#' followed by decimal digits, or
// '#$' followed by hex digits, appending the one character it names to
// Lexeme.  Returns false, having already emitted a diagnostic, if there
// were no digits at all or the value named doesn't fit a Char (0..255,
// Sema.cpp's `maxchar`) -- the caller returns an Error token in that case,
// same as EP's own no-digits/out-of-range nondecimal-literal errors in
// scanNumber above.
bool Scanner::scanControlCodeFragment(std::string& Lexeme) {
    const size_t FragStart = Pos;
    ++Pos; // '#'
    bool Hex = Pos < Text.size() && Text[Pos] == '$';
    if (Hex) ++Pos;
    size_t DigStart = Pos;
    while (Pos < Text.size() &&
           (Hex ? bool(std::isxdigit(static_cast<unsigned char>(Text[Pos])))
                : bool(std::isdigit(static_cast<unsigned char>(Text[Pos])))))
        ++Pos;
    if (Pos == DigStart) {
        emitError(locAt(FragStart), diag::err_control_code_no_digits);
        return false;
    }
    int64_t Value = 0;
    for (size_t I = DigStart; I < Pos; ++I) {
        char C = Text[I];
        int  D = std::isdigit(static_cast<unsigned char>(C)) ? C - '0'
                   : std::tolower(static_cast<unsigned char>(C)) - 'a' + 10;
        // Bailing out the moment Value exceeds 255 (rather than after
        // accumulating the whole run) is what keeps this safe from
        // overflowing Value itself: the multiply below only ever runs
        // against a Value already known to be <= 255.
        Value = Value * (Hex ? 16 : 10) + D;
        if (Value > 255) {
            std::string Digits(Text.substr(DigStart, Pos - DigStart));
            emitError(locAt(FragStart), diag::err_control_code_out_of_range,
                      {std::string_view(Digits)});
            return false;
        }
    }
    Lexeme += static_cast<char>(static_cast<unsigned char>(Value));
    return true;
}

// Scans one Turbo `^ctrl` fragment.  Only ever called once the shape has
// already been confirmed by caretLooksLikeControlChar(), so this cannot
// fail.  ASCII's C0 control convention: the letter's position in the
// alphabet is the code, `^A` = 1 through `^Z` = 26, the same either case.
void Scanner::scanCaretFragment(std::string& Lexeme) {
    ++Pos; // '^'
    char C = Text[Pos++];
    Lexeme += static_cast<char>(std::toupper(static_cast<unsigned char>(C)) - 'A' + 1);
}

bool Scanner::caretLooksLikeControlChar() const {
    return Pos < Text.size() && Text[Pos] == '^' &&
           Pos + 1 < Text.size() &&
           std::isalpha(static_cast<unsigned char>(Text[Pos + 1]));
}

// Turbo `$FF` hexadecimal integer literal.  Converts to a decimal Lexeme
// (like EP's own nondecimal literal above) so every IntLit downstream --
// Parser::parseFactor's std::from_chars call -- can keep assuming base 10
// regardless of which dialect's spelling produced the token.
Token Scanner::scanHexLiteral(size_t TokenStart) {
    ++Pos; // '$'
    size_t DigStart = Pos;
    while (Pos < Text.size() && std::isxdigit(static_cast<unsigned char>(Text[Pos])))
        ++Pos;
    if (Pos == DigStart) {
        emitError(locAt(TokenStart), diag::err_hex_literal_no_digits);
        return make(TokenKind::Error, "$", TokenStart);
    }
    int64_t Value = 0;
    for (size_t I = DigStart; I < Pos; ++I) {
        char C = Text[I];
        int  D = std::isdigit(static_cast<unsigned char>(C)) ? C - '0'
                   : std::tolower(static_cast<unsigned char>(C)) - 'a' + 10;
        // Checked before the multiply, matching the overflow guard EP's own
        // nondecimal literal uses in scanNumber above, and for the same
        // reason: once Value has wrapped there is no recovering the true
        // magnitude from it.
        if (Value > (INT64_MAX - D) / 16) {
            emitError(locAt(TokenStart), diag::err_hex_literal_out_of_range);
            return make(TokenKind::Error,
                        std::string(Text.substr(DigStart, Pos - DigStart)),
                        TokenStart);
        }
        Value = Value * 16 + D;
    }
    return make(TokenKind::IntLit, std::to_string(Value), TokenStart);
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
        // Turbo Pascal gives '@' a different job than ISO 7185's alternative
        // spelling above: a prefix address-of operator (`@x`), unrelated to
        // '^' (postfix dereference, or a pointer type's prefix marker).
        // Under -std=turbo '@' is therefore its own token kind rather than
        // folded into Caret -- this is the one and only place that decision
        // is made, so nothing later in the pipeline has to ask the dialect
        // again.
        case '^':  return make(TokenKind::Caret,        "^", TokenStart);
        case '@':
            if (Opts.turbo())
                return make(TokenKind::At, "@", TokenStart);
            return make(TokenKind::Caret, "@", TokenStart);
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
