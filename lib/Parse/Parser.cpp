//===- Parser.cpp - Parser: construction, token plumbing, and the entry point. ===//

#include "plang/Parse/Parser.h"
#include "ParserInternal.h"
#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"
#include "llvm/Support/Casting.h"

#include <cctype>
#include <charconv>
#include <format>
#include <string>

using namespace plang;

// ---------------------------------------------------------------------------
// Constructor and token-management primitives
// ---------------------------------------------------------------------------

Parser::Parser(Scanner Sc, DiagnosticsEngine& Diags, LangOptions Opts)
    : Opts(Opts), Lex(std::move(Sc)), Diags(Diags) {
    // TypeNames_ is normally populated only as a `type` section's own
    // definitions are parsed (parseTypeDef), since ordinary user types don't
    // exist until declared. The Turbo sized-integer ladder (Sema::
    // registerBuiltins, gated Opts.turbo()) is different: those eleven names
    // (ShortInt/Byte/SmallInt/Word/Cardinal/LongInt/LongWord/Int64/QWord/
    // AnsiChar/Pointer) are predefined Sema symbols the PARSER never sees
    // declared anywhere, so without seeding them here `Byte(SomeWord)` -- an
    // ordinary, common Turbo typecast idiom -- would silently fail to parse
    // as a cast at all (TypeNames_.count("byte") == 0) and fall through to
    // the ordinary CallExpr path, where Sema (which does know about these
    // names) would then reject it as "not callable" instead of accepting the
    // cast. Pre-seeding costs nothing for ISO 7185/Extended Pascal programs,
    // which never reach this branch (every check reading TypeNames_ for cast
    // purposes is itself gated on Opts.turbo()).
    if (Opts.turbo()) {
        static constexpr const char* SizedIntegerLadder[] = {
            "shortint", "byte", "smallint", "word", "cardinal",
            "longint",  "longword", "int64", "qword", "ansichar", "pointer"};
        for (const char* Name : SizedIntegerLadder) TypeNames_.insert(Name);
    }
    advance(); // prime Current with the first token
}

void Parser::emitError(SourceLocation Loc, std::string Msg) {
    ++ErrorCount;
    Diags.report(Loc, DiagSeverity::Error, std::move(Msg));
}

void Parser::emitError(SourceLocation Loc, DiagID ID,
                       std::initializer_list<std::string_view> Args) {
    ++ErrorCount;
    Diags.report(Loc, ID, Args);
}

void Parser::advance() {
    Current = Lex.next();
}

bool Parser::check(TokenKind Kind) const {
    return Current.Kind == Kind;
}

Token Parser::expect(TokenKind Kind) {
    if (Current.Kind == Kind) {
        Token T = Current;
        advance();
        return T;
    }
    // A token with a fixed spelling is named in full by describe(), so quoting
    // its lexeme after it would say the same thing twice: "got 'begin'" rather
    // than "got 'begin' 'begin'".  An identifier or a literal is the other way
    // round — "got identifier" alone does not say which one.
    if (hasFixedSpelling(Current.Kind) || Current.Lexeme.empty()) {
        emitError(Current.toLoc(), diag::err_expected_token,
                  {describe(Kind), describe(Current.Kind)});
    } else {
        emitError(Current.toLoc(), diag::err_expected_token_got_lexeme,
                  {describe(Kind), describe(Current.Kind), Current.Lexeme});
    }
    // Insert-mode recovery: return a synthetic token without consuming Current.
    // This lets the enclosing parse method continue from the same position so
    // an outer scope can usually make sense of what follows.
    return Token{Kind, "", Current.Loc};
}

bool Parser::match(TokenKind Kind) {
    if (Current.Kind == Kind) {
        advance();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

std::unique_ptr<ProgramNode> Parser::parse() {
    // EP §6.11: a file may begin with module definitions.
    if (Opts.extendedPascal() && check(TokenKind::Module)) {
        auto Prog = parseMultiUnitFile();
        return (ErrorCount > 0) ? nullptr : std::move(Prog);
    }
    auto Prog = parseProgram();
    return (ErrorCount > 0) ? nullptr : std::move(Prog);
}
