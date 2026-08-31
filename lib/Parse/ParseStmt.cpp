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

        // Turbo VARIABLE typecast rooted in one of the five standard type
        // names that are lexer KEYWORDS rather than Identifier tokens --
        // Integer(x) := ..., Real(x) := ..., etc. -- so the 'Identifier'
        // case just below never sees them; mirrors parseFactor's identical
        // keyword arm (ParseExpr.cpp).  Not Turbo, or not followed by '(':
        // none of these keywords begins a statement of any other shape, so
        // this falls out exactly the way it always has -- as the 'default'
        // arm's empty-statement (epsilon) production, with the leftover
        // token reported by whatever enclosing construct expected a
        // separator or a closing keyword next.
        case TokenKind::Integer:
        case TokenKind::Real:
        case TokenKind::Boolean:
        case TokenKind::Char:
        case TokenKind::String: {
            if (!Opts.turbo()) return nullptr;
            Token KwTok = Current;
            advance();
            if (!check(TokenKind::LeftParen)) return nullptr;
            advance(); // consume '('
            auto Node      = std::make_unique<TypeCastExpr>();
            Node->Loc      = KwTok;
            Node->TypeName = KwTok.Lexeme;
            Node->Operand  = parseExpression();
            expect(TokenKind::RightParen);
            auto Lval = parsePostfix(std::move(Node));

            if (check(TokenKind::Assign)) {
                advance();
                auto AssignNode    = std::make_unique<AssignStmt>();
                AssignNode->Loc    = Lval->Loc;
                AssignNode->Target = std::move(Lval);
                AssignNode->Value  = parseExpression();
                return AssignNode;
            }
            // Neither a label target (only an identifier or integer may be
            // one) nor a procedure name (CallStmt needs an IdentExpr) can
            // ever be built from one of these keywords, so unlike the
            // Identifier case just below, there is only the one shape to
            // fall back to here.
            emitError(Lval->Loc, diag::err_expected_assign_after_var);
            return nullptr;
        }

        case TokenKind::Identifier: {
            Token IdentTok = Current;
            advance();

            // Turbo VARIABLE typecast as the ROOT of an lvalue path:
            // TypeName(expr) reinterprets expr's own storage in place, so
            // `TByteRec(SomeWord).Lo := 0` mutates SomeWord itself rather
            // than a copy -- see TypeCastExpr's own comment (AstExpr.h).
            // Recognized by the exact same TypeNames_/VarNames_ test
            // parseFactor's identifier branch uses for the value-cast form
            // (ParseExpr.cpp), so the two never disagree about which of the
            // two grammars a given 'TypeName(' begins. Once built, this
            // feeds into parsePostfix exactly like an ordinary IdentExpr
            // would, so `.field`/`[i]`/`^` chain onto it the same way.
            std::unique_ptr<ExprNode> Lval;
            {
                const std::string Lower = toLower(IdentTok.Lexeme);
                const bool NamesAType = Opts.turbo() && TypeNames_.count(Lower)
                                         && !VarNames_.count(Lower);
                if (NamesAType && check(TokenKind::LeftParen)) {
                    advance(); // consume '('
                    auto Node      = std::make_unique<TypeCastExpr>();
                    Node->Loc      = IdentTok;
                    Node->TypeName = IdentTok.Lexeme;
                    Node->Operand  = parseExpression();
                    expect(TokenKind::RightParen);
                    Lval = parsePostfix(std::move(Node));
                }
            }
            if (!Lval) {
                // Build IdentExpr then apply any postfix operators to form the LValue.
                auto Ident  = std::make_unique<IdentExpr>();
                Ident->Loc  = IdentTok;
                Ident->Name = IdentTok.Lexeme;
                // EP §6.11.2: M.name for a module imported `qualified` is one
                // name, not a field selection.
                if (check(TokenKind::Dot)
                        && QualifiedModules_.count(toLower(Ident->Name))) {
                    advance();
                    Ident->Name += "." + expect(TokenKind::Identifier).Lexeme;
                }
                Lval = parsePostfix(std::move(Ident));
            }

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
                    // canonicalLabel, not Id->Name/IdentTok.Lexeme directly:
                    // every other site that produces a label spelling (the
                    // label-section declaration, a goto's target, an integer
                    // labeled-statement) goes through it too, and it is what
                    // lower-cases an identifier label so this one still
                    // matches its declaration's spelling in
                    // Sema's CurrentBlockLabels/LabelEnclosingStmt and
                    // CodeGen's LabelGotoEngine, all of which key on this
                    // string by plain equality.
                    Node->Label = canonicalLabel(IdentTok.Lexeme);
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
                        // TP-only: Str(x [:width[:decimals]], var s) formats
                        // its FIRST argument the same way write/writestr's
                        // value arguments do; the second (destination) never
                        // has a colon suffix, but parseWriteArg is safe to
                        // apply to it too since the ':' is only consumed
                        // when actually present (checked, not assumed).
                        bool IsWrite = (Lo == "write" || Lo == "writeln"
                                        || Lo == "writestr" || Lo == "str");
                        Node->Args.push_back(IsWrite ? parseWriteArg() : parseExpression());
                        while (match(TokenKind::Comma)) {
                            Node->Args.push_back(IsWrite ? parseWriteArg() : parseExpression());
                        }
                    }
                    expect(TokenKind::RightParen);
                }
                return Node;
            }

            // Turbo Tier 5, Cluster A item 3: 'Obj.Method(args);' -- Lval is
            // already a MethodCallExpr (parsePostfix built one the moment it
            // saw '.identifier(' -- see that function's own comment,
            // ParseExpr.cpp) -- becomes a MethodCallStmt the same way an
            // ordinary CallExpr-shaped bare identifier becomes a CallStmt
            // just above, just with a Receiver instead of a bare Name.
            if (auto* Mc = llvm::dyn_cast<MethodCallExpr>(Lval.get())) {
                auto Node      = std::make_unique<MethodCallStmt>();
                Node->Loc      = Mc->Loc;
                Node->Receiver = std::move(Mc->Receiver);
                Node->Method   = std::move(Mc->Method);
                Node->Args     = std::move(Mc->Args);
                return Node;
            }
            // Turbo Tier 5, Cluster A item 3: the BARE form, 'Obj.Method;'
            // with no parens at all -- confirmed legal against a local
            // fpc -Mtp build, the identical relaxation a bare 'Foo;' already
            // gets for a zero-argument ordinary procedure.  Lval is a plain
            // FieldExpr here (parsePostfix has no way to know '.identifier'
            // with nothing after it is a method rather than a field read
            // used, illegally, as a statement -- there is no such thing as
            // an expression-statement in this grammar otherwise, so this was
            // always an error before and Sema now decides which one).
            if (auto* Fe = llvm::dyn_cast<FieldExpr>(Lval.get())) {
                auto Node      = std::make_unique<MethodCallStmt>();
                Node->Loc      = Fe->Loc;
                Node->Receiver = std::move(Fe->Record);
                Node->Method   = Fe->Field;
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
            if (Opts.turbo()) {
                // Turbo's else/otherwise part is a STATEMENT LIST bounded by
                // the case-statement's own 'end', the same shape a
                // 'begin ... end' block's body has, just without the
                // 'begin'/'end' keywords themselves (the case-statement's
                // 'end' already closes it).  Parsed with the identical loop
                // parseCompoundStmt uses for its own statement list, and
                // wrapped in a CompoundStmt purely as a carrier so
                // CaseStmt::Else stays exactly one child node whether the
                // dialect is Turbo (a list) or ISO10206 (a single statement,
                // below).
                auto Seq = std::make_unique<CompoundStmt>();
                Seq->Loc  = Current;
                auto Stmt = parseStatement();
                if (Stmt) Seq->Stmts.push_back(std::move(Stmt));
                while (match(TokenKind::Semicolon)) {
                    auto S = parseStatement();
                    if (S) Seq->Stmts.push_back(std::move(S));
                }
                Node->Else = std::move(Seq);
            } else {
                Node->Else = parseStatement();
                match(TokenKind::Semicolon);
            }
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
