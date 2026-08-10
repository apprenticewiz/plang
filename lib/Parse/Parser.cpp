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
