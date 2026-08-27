//===- ParseStmt.cpp - Parsing of statements (§6.8). ===//

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

// Ceiling on live parseStatement activations (Parser::StmtDepth).  Mirrors
// MaxExprDepth in ParseExpr.cpp: 500 levels of nesting is nowhere near
// exhausting an 8MB default stack -- every construct that recurses through
// parseStatement (nested 'begin ... end', if-then-else chains, while/for/
// repeat/with bodies, case arms) crashes only tens of thousands of levels
// deeper than this on this machine -- while no legitimate Extended Pascal
// program nests statements anywhere close to this deep.  Without a ceiling
// here, a source file built specifically to nest deeply (or a generated/
// fuzzed one) drives the real call stack instead of a diagnostic.
static constexpr unsigned MaxStmtDepth = 500;

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// statement → assignment | compound-stmt | if-stmt | while-stmt | for-stmt
//           | repeat-stmt | with-stmt | goto-stmt | labeled-stmt
//           | call-stmt | ε
std::unique_ptr<StmtNode> Parser::parseStatement() {
    // Every recursive re-entry into statement parsing -- a compound
    // statement's members, an if/while/for/repeat/with statement's body, a
    // case arm, a labeled statement's target -- funnels through this
    // activation, so this is the one place a ceiling bounds the whole cycle.
    // Checked before the RAII bump a few lines down: a caller already sitting
    // at the ceiling must return without recursing again, not recurse once
    // more and only then stop.
    if (StmtDepth >= MaxStmtDepth) {
        if (!StmtDepthLimitHit) {
            StmtDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_stmt_too_deeply_nested);
        }
        // Deliberately does not consume Current, and returns nullptr the same
        // way the ordinary ε production does -- every caller already handles
        // a null statement, and every caller up the stack is still waiting on
        // its own closing token ('end', 'until', etc.) and unwinds on its own
        // once it sees the same token still there.
        return nullptr;
    }
    StmtDepthScope DepthGuard(StmtDepth, StmtDepthLimitHit);

    switch (Current.Kind) {
        case TokenKind::Begin:
            return parseCompoundStmt();

        case TokenKind::If:
            return parseIfStmt();

        case TokenKind::While:
            return parseWhileStmt();

        case TokenKind::For:
            return parseForStmt();

        case TokenKind::Repeat:
            return parseRepeatStmt();

        case TokenKind::With:
            return parseWithStmt();

        case TokenKind::Case:
            return parseCaseStmt();

        case TokenKind::Goto: {
            auto Node = std::make_unique<GotoStmt>();
            Node->Loc = Current;
            advance();
            if (check(TokenKind::IntLit) || check(TokenKind::Identifier)) {
                Node->Label = canonicalLabel(Current.Lexeme);
                advance();
            } else {
                emitError(Current.toLoc(), diag::err_expected_goto_label,
                          {describe(Current.Kind)});
            }
            return Node;
        }

        // Integer label: 99: statement
        case TokenKind::IntLit: {
            Token LabelTok = Current;
            advance();
            if (match(TokenKind::Colon)) {
                auto Node   = std::make_unique<LabeledStmt>();
                Node->Loc   = LabelTok;
                Node->Label = canonicalLabel(LabelTok.Lexeme);
                Node->Stmt  = parseStatement();
                return Node;
            }
            emitError(LabelTok.toLoc(), diag::err_unexpected_int_literal);
            return nullptr;
        }

        case TokenKind::Identifier: {
            Token IdentTok = Current;
            advance();

            // Build IdentExpr then apply any postfix operators to form the LValue.
            auto Ident  = std::make_unique<IdentExpr>();
            Ident->Loc  = IdentTok;
            Ident->Name = IdentTok.Lexeme;
            // EP §6.11.2: M.name for a module imported `qualified` is one name,
            // not a field selection.
            if (check(TokenKind::Dot)
                    && QualifiedModules_.count(toLower(Ident->Name))) {
                advance();
                Ident->Name += "." + expect(TokenKind::Identifier).Lexeme;
            }
            auto Lval   = parsePostfix(std::move(Ident));

            if (check(TokenKind::Assign)) {
                // assignment → lvalue ':=' expression
                advance();
                auto Node    = std::make_unique<AssignStmt>();
                Node->Loc    = Lval->Loc;
                Node->Target = std::move(Lval);
                Node->Value  = parseExpression();
                return Node;
            }

            if (auto* Id = llvm::dyn_cast<IdentExpr>(Lval.get())) {
                // Bare identifier followed by ':' → labeled statement (identifier label).
                if (check(TokenKind::Colon)) {
                    advance();
                    auto Node   = std::make_unique<LabeledStmt>();
                    Node->Loc   = IdentTok;
                    Node->Label = Id->Name;
                    Node->Stmt  = parseStatement();
                    return Node;
                }
                // Otherwise it's a procedure call.
                auto Node  = std::make_unique<CallStmt>();
                Node->Loc  = IdentTok;
                Node->Name = Id->Name;
                if (match(TokenKind::LeftParen)) {
                    if (!check(TokenKind::RightParen)) {
                        // write/writeln arguments support ':' width/decimal
                        // specifiers, and so does writestr (EP §6.7.5.5, whose
                        // parameter list is defined in terms of write-parameters).
                        std::string Lo = Id->Name;
                        for (auto& C : Lo) C = static_cast<char>(std::tolower(
                                                  static_cast<unsigned char>(C)));
                        bool IsWrite = (Lo == "write" || Lo == "writeln"
                                        || Lo == "writestr");
                        Node->Args.push_back(IsWrite ? parseWriteArg() : parseExpression());
                        while (match(TokenKind::Comma)) {
                            Node->Args.push_back(IsWrite ? parseWriteArg() : parseExpression());
                        }
                    }
                    expect(TokenKind::RightParen);
                }
                return Node;
            }

            emitError(Lval->Loc, diag::err_expected_assign_after_var);
            return nullptr;
        }

        default:
            // ε — empty statement; the caller is responsible for handling nullptr.
            return nullptr;
    }
}

// compound-stmt → 'begin' statement (';' statement)* 'end'
std::unique_ptr<CompoundStmt> Parser::parseCompoundStmt() {
    auto Node = std::make_unique<CompoundStmt>();
    Node->Loc = Current;
    expect(TokenKind::Begin);

    auto Stmt = parseStatement();
    if (Stmt) Node->Stmts.push_back(std::move(Stmt));

    while (match(TokenKind::Semicolon)) {
        auto S = parseStatement();
        if (S) Node->Stmts.push_back(std::move(S));
    }

    // Suppressed while unwinding from the depth ceiling above: every
    // enclosing 'begin' between here and the ceiling is missing its 'end'
    // for the same reason, and expect()'s diagnostic once per level would
    // bury the one diagnostic that actually explains the failure.
    if (StmtDepthLimitHit)
        match(TokenKind::End);
    else
        expect(TokenKind::End);
    return Node;
}

// if-stmt → 'if' expression 'then' statement ('else' statement)?
std::unique_ptr<IfStmt> Parser::parseIfStmt() {
    auto Node = std::make_unique<IfStmt>();
    Node->Loc = Current;
    expect(TokenKind::If);
    Node->Cond = parseExpression();
    expect(TokenKind::Then);
    Node->Then = parseStatement();
    if (match(TokenKind::Else)) {
        Node->Else = parseStatement();
    }
    return Node;
}

// while-stmt → 'while' expression 'do' statement
std::unique_ptr<WhileStmt> Parser::parseWhileStmt() {
    auto Node = std::make_unique<WhileStmt>();
    Node->Loc = Current;
    expect(TokenKind::While);
    Node->Cond = parseExpression();
    expect(TokenKind::Do);
    Node->Body = parseStatement();
    return Node;
}

// for-stmt → 'for' identifier ':=' expression ('to' | 'downto') expression 'do' statement
//           | 'for' identifier 'in' set-expression 'do' statement  (EP §6.9.3.9.3)
std::unique_ptr<StmtNode> Parser::parseForStmt() {
    Token Loc = Current;
    expect(TokenKind::For);
    std::string Var = expect(TokenKind::Identifier).Lexeme;

    // EP §6.9.3.9.3: for v in set-expr do
    if (check(TokenKind::In)) {
        // 'in' is a required word under both standards, being the membership
        // operator, so only this says the set form is not standard Pascal's.
        if (!Opts.extendedPascal())
            emitError(Current.toLoc(), diag::err_ep_for_in);
        advance();
        auto Node    = std::make_unique<ForInStmt>();
        Node->Loc    = Loc;
        Node->Var    = std::move(Var);
        Node->SetExpr = parseExpression();
        expect(TokenKind::Do);
        Node->Body   = parseStatement();
        return Node;
    }

    // ISO 7185: for v := from to/downto limit do
    auto Node = std::make_unique<ForStmt>();
    Node->Loc  = Loc;
    Node->Var  = std::move(Var);
    expect(TokenKind::Assign);
    Node->From = parseExpression();
    if (match(TokenKind::To)) {
        Node->Downto = false;
    } else if (match(TokenKind::Downto)) {
        Node->Downto = true;
    } else {
        emitError(Current.toLoc(), diag::err_expected_to_or_downto,
                  {describe(Current.Kind)});
        Node->Downto = false;
    }
    Node->Limit = parseExpression();
    expect(TokenKind::Do);
    Node->Body  = parseStatement();
    return Node;
}

// repeat-stmt → 'repeat' statement (';' statement)* 'until' expression
std::unique_ptr<RepeatStmt> Parser::parseRepeatStmt() {
    auto Node = std::make_unique<RepeatStmt>();
    Node->Loc = Current;
    expect(TokenKind::Repeat);

    auto Stmt = parseStatement();
    if (Stmt) Node->Stmts.push_back(std::move(Stmt));
    while (match(TokenKind::Semicolon)) {
        auto S = parseStatement();
        if (S) Node->Stmts.push_back(std::move(S));
    }

    expect(TokenKind::Until);
    Node->Cond = parseExpression();
    return Node;
}

// with-stmt → 'with' variable (',' variable)* 'do' statement
std::unique_ptr<WithStmt> Parser::parseWithStmt() {
    auto Node = std::make_unique<WithStmt>();
    Node->Loc = Current;
    expect(TokenKind::With);

    Node->Records.push_back(parseFactor());
    while (match(TokenKind::Comma)) {
        Node->Records.push_back(parseFactor());
    }

    expect(TokenKind::Do);
    Node->Body = parseStatement();
    return Node;
}

// case-stmt → 'case' expression 'of' case-arm (';' case-arm)* [';'] 'end'
// case-arm  → case-constant {',' case-constant} ':' statement
std::unique_ptr<CaseStmt> Parser::parseCaseStmt() {
    auto Node = std::make_unique<CaseStmt>();
    Node->Loc = Current;
    expect(TokenKind::Case);
    Node->Selector = parseExpression();
    expect(TokenKind::Of);

    while (!check(TokenKind::End) && !check(TokenKind::Eof)) {
        // 'otherwise' (EP §6.9.3.5), or 'else' spelling the same thing.  A
        // standard Pascal case-statement has no such part at all: the value
        // must match one of the case-constants.  'otherwise' is not a word
        // there, so it is 'else' that would otherwise slip through.
        if (check(TokenKind::Else) || check(TokenKind::Otherwise)) {
            if (!Opts.has(LangOptions::Feature::CaseDefaultArm))
                emitError(Current.toLoc(), diag::err_ep_case_otherwise);
            advance();
            Node->HasElse = true;
            Node->Else    = parseStatement();
            match(TokenKind::Semicolon);
            break;
        }

        CaseArm Arm;
        // Parse comma-separated case labels; each may be a point or lo..hi range.
        auto parseCaseLabel = [&]() -> CaseLabel {
            CaseLabel Lbl;
            // ISO §6.8.3.5 / §6.3: a case-constant may carry a sign.
            Lbl.Low = parseCaseConstant();
            // EP §6.9.3.5: a case-constant may be a range.  Standard Pascal's
            // is one constant, and every value has to be written out.
            if (check(TokenKind::DotDot)) {
                if (!Opts.has(LangOptions::Feature::CaseConstantRanges))
                    emitError(Current.toLoc(), diag::err_ep_case_range);
                advance();
                Lbl.High = parseCaseConstant();
            }
            return Lbl;
        };
        Arm.Labels.push_back(parseCaseLabel());
        while (match(TokenKind::Comma)) {
            Arm.Labels.push_back(parseCaseLabel());
        }
        expect(TokenKind::Colon);
        Arm.Body = parseStatement();
        Node->Arms.push_back(std::move(Arm));

        // EP §6.9.3.5: case-statement = ... ( case-list-element { ';' case-
        // list-element } [ [ ';' ] case-statement-completer ] | ... ) [ ';' ]
        // 'end' .  The semicolon is MANDATORY between two ordinary case-list-
        // elements, but OPTIONAL immediately before the completer
        // ('otherwise'/'else').  Loop back without requiring one when that's
        // what follows, so the check at the top of the loop gets a chance to
        // fire instead of unconditionally breaking to expect(End) below.
        if (!match(TokenKind::Semicolon)) {
            if (check(TokenKind::Otherwise) || check(TokenKind::Else))
                continue;
            break;
        }
        // A trailing semicolon before 'end' is fine — just loop around.
    }

    expect(TokenKind::End);
    return Node;
}

// Parse one write/writeln argument: expression [':' width [':' decimals]]
std::unique_ptr<ExprNode> Parser::parseWriteArg() {
    auto Val = parseExpression();
    if (!check(TokenKind::Colon)) return Val;

    // Has width specifier.
    auto Wp    = std::make_unique<WriteParam>();
    Wp->Loc    = Val->Loc;
    Wp->Value  = std::move(Val);
    advance(); // consume ':'
    Wp->Width = parseExpression();

    if (match(TokenKind::Colon)) {
        Wp->Decimals = parseExpression();
    }
    return Wp;
}
