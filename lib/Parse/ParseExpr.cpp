//===- ParseExpr.cpp - Parsing of expressions (§6.7). ===//

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

// Returns true for the seven relational operators (including 'in').
static bool isRelop(TokenKind K) {
    switch (K) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::LessThan:
        case TokenKind::LessThanOrEqual:
        case TokenKind::GreaterThan:
        case TokenKind::GreaterThanOrEqual:
        case TokenKind::In:
            return true;
        default:
            return false;
    }
}

// Returns true for the additive operators: + - or or_else >< (EP)
static bool isAddop(TokenKind K) {
    switch (K) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Or:
        case TokenKind::OrElse:   // EP §6.8.3.3
        case TokenKind::SymDiff:  // EP §6.8.3.4
            return true;
        default:
            return false;
    }
}

// Returns true for the multiplicative operators: * / div mod and and_then (EP)
static bool isMulop(TokenKind K) {
    switch (K) {
        case TokenKind::Times:
        case TokenKind::Divide:
        case TokenKind::Div:
        case TokenKind::Mod:
        case TokenKind::And:
        case TokenKind::AndThen:  // EP §6.8.3.3
            return true;
        default:
            return false;
    }
}

// Returns true for the exponentiating operators: ** pow (EP §6.8.3.2)
static bool isExpop(TokenKind K) {
    return K == TokenKind::StarStar || K == TokenKind::Pow;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// expression → simple-expr ( relop simple-expr )?
std::unique_ptr<ExprNode> Parser::parseExpression() {
    auto Left = parseSimpleExpr();
    if (isRelop(Current.Kind)) {
        auto Node  = std::make_unique<BinaryExpr>();
        Node->Loc  = Current;
        Node->Op   = Current.Kind;
        advance();
        Node->Left  = std::move(Left);
        Node->Right = parseSimpleExpr();
        return Node;
    }
    return Left;
}

// simple-expr → ('+' | '-')? term (addop term)*
std::unique_ptr<ExprNode> Parser::parseSimpleExpr() {
    std::unique_ptr<ExprNode> Left;
    if (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        auto Node     = std::make_unique<UnaryExpr>();
        Node->Loc     = Current;
        Node->Op      = Current.Kind;
        advance();
        Node->Operand = parseTerm();
        Left = std::move(Node);
    } else {
        Left = parseTerm();
    }

    while (isAddop(Current.Kind)) {
        // Symmetric set difference is Extended Pascal's, and unlike 'or_else'
        // beside it, it is spelt as a symbol rather than a word, so nothing
        // else keeps it out of standard Pascal.
        if (Current.Kind == TokenKind::SymDiff && !Opts.extendedPascal())
            emitError(Current.toLoc(), diag::err_ep_operator, {"><"});
        auto Node  = std::make_unique<BinaryExpr>();
        Node->Loc  = Current;
        Node->Op   = Current.Kind;
        advance();
        Node->Left  = std::move(Left);
        Node->Right = parseTerm();
        Left = std::move(Node);
    }

    return Left;
}

// power → factor ('**' | 'pow' factor)?   EP §6.8.3.2 — right-associative
std::unique_ptr<ExprNode> Parser::parsePower() {
    auto Left = parseFactor();
    if (isExpop(Current.Kind)) {
        // Standard Pascal has no exponentiation operator.  'pow' is already a
        // plain identifier there, but '**' is a symbol and would otherwise be
        // read as one under either standard.
        if (!Opts.extendedPascal())
            emitError(Current.toLoc(), diag::err_ep_operator, {Current.Lexeme});
        auto Node  = std::make_unique<BinaryExpr>();
        Node->Loc  = Current;
        Node->Op   = Current.Kind;
        advance();
        Node->Left  = std::move(Left);
        Node->Right = parsePower();   // right-associative
        return Node;
    }
    return Left;
}

// term → power (mulop power)*
std::unique_ptr<ExprNode> Parser::parseTerm() {
    auto Left = parsePower();

    while (isMulop(Current.Kind)) {
        auto Node  = std::make_unique<BinaryExpr>();
        Node->Loc  = Current;
        Node->Op   = Current.Kind;
        advance();
        Node->Left  = std::move(Left);
        Node->Right = parsePower();
        Left = std::move(Node);
    }

    return Left;
}

// Applies zero or more postfix operators to Expr:
//   '[' expression ']' → IndexExpr
//   '.' identifier      → FieldExpr
//   '^'                 → DerefExpr
std::unique_ptr<ExprNode> Parser::parsePostfix(std::unique_ptr<ExprNode> Expr) {
    for (;;) {
        if (check(TokenKind::LeftBracket)) {
            Token Loc = Current;
            advance();
            // ISO §6.5.3.2: a[i, j] abbreviates a[i][j], which is what the
            // matching abbreviation in the array type denotes.
            do {
                auto IdxOrLow = parseExpression();
                if (match(TokenKind::DotDot)) {
                    // EP §6.5.6: s[i..j] — substring variable
                    auto Node  = std::make_unique<SubstringExpr>();
                    Node->Loc  = Loc;
                    Node->Str  = std::move(Expr);
                    Node->Low  = std::move(IdxOrLow);
                    Node->High = parseExpression();
                    Expr = std::move(Node);
                } else {
                    auto Node   = std::make_unique<IndexExpr>();
                    Node->Loc   = Loc;
                    Node->Array = std::move(Expr);
                    Node->Index = std::move(IdxOrLow);
                    Expr = std::move(Node);
                }
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightBracket);
        } else if (check(TokenKind::Dot)) {
            Token Loc = Current;
            advance();
            auto Node    = std::make_unique<FieldExpr>();
            Node->Loc    = Loc;
            Node->Record = std::move(Expr);
            Node->Field  = expect(TokenKind::Identifier).Lexeme;
            Expr = std::move(Node);
        } else if (check(TokenKind::Caret)) {
            Token Loc = Current;
            advance();
            auto Node     = std::make_unique<DerefExpr>();
            Node->Loc     = Loc;
            Node->Pointer = std::move(Expr);
            Expr = std::move(Node);
        } else {
            break;
        }
    }
    return Expr;
}

// factor → literals | 'nil' | 'not' factor | '(' expression ')'
//        | '[' set-elements ']' | identifier ( '(' args ')' | postfix* )
std::unique_ptr<ExprNode> Parser::parseFactor() {
    Token Loc = Current;

    switch (Current.Kind) {
        case TokenKind::IntLit: {
            auto Node = std::make_unique<IntLitExpr>();
            Node->Loc = Loc;
            const auto& S = Current.Lexeme;
            auto [Ptr, Ec] = std::from_chars(S.data(), S.data() + S.size(), Node->Value);
            if (Ec == std::errc::result_out_of_range) {
                emitError(Loc.toLoc(), diag::err_int_literal_out_of_range, {S});
                Node->Value = 0;
            }
            advance();
            return Node;
        }

        case TokenKind::RealLit: {
            auto Node = std::make_unique<RealLitExpr>();
            Node->Loc = Loc;
            const auto& S = Current.Lexeme;
            // Not std::stod, which throws on a scale-factor too large to
            // represent — reported here the way an out-of-range integer is.
            auto [Ptr, Ec] = std::from_chars(S.data(), S.data() + S.size(),
                                             Node->Value);
            if (Ec == std::errc::result_out_of_range) {
                emitError(Loc.toLoc(), diag::err_real_literal_out_of_range, {S});
                Node->Value = 0.0;
            }
            advance();
            return Node;
        }

        case TokenKind::StringLit: {
            auto Node   = std::make_unique<StringLitExpr>();
            Node->Loc   = Loc;
            Node->Value = Current.Lexeme;
            advance();
            return Node;
        }

        case TokenKind::Nil: {
            auto Node = std::make_unique<NilExpr>();
            Node->Loc = Loc;
            advance();
            return Node;
        }

        case TokenKind::True: {
            auto Node   = std::make_unique<BoolLitExpr>();
            Node->Loc   = Loc;
            Node->Value = true;
            advance();
            return Node;
        }

        case TokenKind::False: {
            auto Node   = std::make_unique<BoolLitExpr>();
            Node->Loc   = Loc;
            Node->Value = false;
            advance();
            return Node;
        }

        case TokenKind::Not: {
            auto Node     = std::make_unique<UnaryExpr>();
            Node->Loc     = Loc;
            Node->Op      = TokenKind::Not;
            advance();
            Node->Operand = parseFactor();
            return Node;
        }

        case TokenKind::LeftParen: {
            advance();
            auto Expr = parseExpression();
            expect(TokenKind::RightParen);
            return Expr;
        }

        // Set literal: '[' element (',' element)* ']'
        // where element is expr | expr '..' expr
        case TokenKind::LeftBracket: {
            auto Node = std::make_unique<SetLiteralExpr>();
            Node->Loc = Loc;
            advance();
            if (!check(TokenKind::RightBracket)) {
                auto Elem = parseExpression();
                if (match(TokenKind::DotDot)) {
                    auto Range  = std::make_unique<SetRangeExpr>();
                    Range->Loc  = Elem->Loc;
                    Range->Low  = std::move(Elem);
                    Range->High = parseExpression();
                    Node->Elements.push_back(std::move(Range));
                } else {
                    Node->Elements.push_back(std::move(Elem));
                }
                while (match(TokenKind::Comma)) {
                    auto E = parseExpression();
                    if (match(TokenKind::DotDot)) {
                        auto Range  = std::make_unique<SetRangeExpr>();
                        Range->Loc  = E->Loc;
                        Range->Low  = std::move(E);
                        Range->High = parseExpression();
                        Node->Elements.push_back(std::move(Range));
                    } else {
                        Node->Elements.push_back(std::move(E));
                    }
                }
            }
            expect(TokenKind::RightBracket);
            return Node;
        }

        case TokenKind::Identifier: {
            std::string Name = Current.Lexeme;
            advance();
            // EP §6.11.2: a name imported `qualified` is written M.name, which
            // is otherwise indistinguishable from a field of a record M.
            if (check(TokenKind::Dot)
                    && QualifiedModules_.count(toLower(Name))) {
                advance(); // consume '.'
                Name += "." + expect(TokenKind::Identifier).Lexeme;
            }

            if (match(TokenKind::LeftParen)) {
                auto Node  = std::make_unique<CallExpr>();
                Node->Loc  = Loc;
                Node->Name = Name;
                if (!check(TokenKind::RightParen)) {
                    Node->Args.push_back(parseExpression());
                    while (match(TokenKind::Comma)) {
                        Node->Args.push_back(parseExpression());
                    }
                }
                expect(TokenKind::RightParen);
                // EP §6.7.2: a function may return a record, an array or a
                // pointer, and then selecting from the result is how it is
                // read — binding(f).bound is the whole point of binding.
                return parsePostfix(std::move(Node));
            } else if (Opts.extendedPascal() && check(TokenKind::LeftBracket)) {
                // EP §6.8.7: TypeName[...] could be a structured value constructor.
                // parseStructuredValueOrIndex decides based on bracket contents.
                return parseStructuredValueOrIndex(Name, Loc);
            } else {
                // Variable reference — apply any trailing postfix operators.
                auto Ident  = std::make_unique<IdentExpr>();
                Ident->Loc  = Loc;
                Ident->Name = Name;
                return parsePostfix(std::move(Ident));
            }
        }

        default: {
            emitError(Loc.toLoc(), diag::err_expected_expr, {describe(Current.Kind)});
            // Consume-mode recovery: advance past the unexpected token to prevent
            // the enclosing expression loop from spinning on the same token.
            if (Current.Kind != TokenKind::Eof) advance();
            auto Node   = std::make_unique<IntLitExpr>();
            Node->Loc   = Loc;
            Node->Value = 0;
            return Node;
        }
    }
}
