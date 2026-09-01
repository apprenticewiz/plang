//===- ParseExpr.cpp - Parsing of expressions (§6.7). ===//

#include "plang/Parse/Parser.h"
#include "ParserInternal.h"
#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ProgramStack.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
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

// Returns true for the additive operators: + - or or_else >< (EP) xor (TP)
static bool isAddop(TokenKind K) {
    switch (K) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Or:
        case TokenKind::OrElse:   // EP §6.8.3.3
        case TokenKind::SymDiff:  // EP §6.8.3.4
        // Turbo Pascal's own operator table puts 'xor' at the same
        // precedence tier as 'or': both ADDOPs.  No explicit dialect check
        // is needed here -- Xor is a DIALECT_KEYWORD gated to D_Turbo alone
        // (TokenKinds.def), so the scanner hands 'xor' back as a plain
        // Identifier under every other dialect and this case is simply
        // never reached there (the same reasoning AndThen/OrElse below rely
        // on for their own EP-only gating).
        case TokenKind::Xor:      // TP7 operator table
            return true;
        default:
            return false;
    }
}

// Returns true for the multiplicative operators: * / div mod and and_then
// (EP) shl shr (TP)
static bool isMulop(TokenKind K) {
    switch (K) {
        case TokenKind::Times:
        case TokenKind::Divide:
        case TokenKind::Div:
        case TokenKind::Mod:
        case TokenKind::And:
        case TokenKind::AndThen:  // EP §6.8.3.3
        // Turbo Pascal's own operator table puts 'shl'/'shr' at the same
        // precedence tier as '*': both MULOPs.  Same scanner-gate reasoning
        // as 'xor' in isAddop above -- Shl/Shr are D_Turbo-only keywords, so
        // no explicit dialect check belongs here either.
        case TokenKind::Shl:      // TP7 operator table
        case TokenKind::Shr:      // TP7 operator table
            return true;
        default:
            return false;
    }
}

// Returns true for the exponentiating operators: ** pow (EP §6.8.3.2)
static bool isExpop(TokenKind K) {
    return K == TokenKind::StarStar || K == TokenKind::Pow;
}

// Ceiling on live parseFactor activations (Parser::ExprDepth).  500 levels of
// nesting costs about 2500 C++ frames -- five per level through
// parseExpression -> parseSimpleExpr -> parseTerm -> parsePower -> parseFactor
// -- nowhere near exhausting an 8MB default stack, while no legitimate
// Extended Pascal program nests expressions anywhere close to this deep.
// Without a ceiling here, a source file built specifically to nest deeply (or
// a generated/fuzzed one) drives the real call stack instead of a diagnostic.
static constexpr unsigned MaxExprDepth = 500;

// Ceiling on the safety margin subtracted from the platform's real stack
// budget before parsePower's own recursion (below) treats itself as "nearly
// exhausted". Covers: (a) whatever of the thread's stack was already spent
// between process start and Parser::StackBaseline being captured (a short,
// bounded, non-recursive chain of frames through main()/the driver/Frontend
// down to Parser's own constructor -- typically well under 100KB, never
// close to this margin on a normal-sized stack), and (b) headroom for
// everything that still has to run on this same stack once parsePower stops
// recursing: unwinding back out through parseTerm/parseSimpleExpr/
// parseExpression, diagnostic emission, and (issue #551's own concern) AST
// teardown of whatever chain was already built. 1 MiB is generous for all of
// that against an 8MiB default stack while still leaving the overwhelming
// majority of the budget available to recurse into -- see
// powerStackNearlyExhausted's own comment for why the exact number here is
// far less load-bearing than it would be for a term-count ceiling: get it
// somewhat wrong in either direction and the worst case is rejecting a chain
// a few thousand terms earlier or later than strictly necessary, not
// silently narrowing accepted syntax the way reusing MaxExprDepth above for
// this did (PR #553, reverted; see issue #300's reopening comment).
//
// This is a *ceiling*, not the margin actually used: powerStackNearlyExhausted
// scales it down for unusually small budgets (see that function's own
// comment) so that a small-but-real platform stack limit -- a constrained
// container, a hardened deployment, or (this PR's own 3rd commit) a
// parser-fuzzer worker's stack, any of which can be at or below this many
// bytes -- cannot make the check treat itself as "already exhausted"
// independent of how much of the budget is actually in use (found in PR
// #555's own review, after the commit below first wired this check into a
// context where such small budgets are realistic).
static constexpr size_t PowerStackSafetyMargin = 1u << 20; // 1 MiB

// True once continuing to recurse into parsePower (below) would risk
// running the real C++ call stack past the platform's own limit.
//
// parsePower's right-associative recursion for a '**'/'pow' chain (EP
// §6.8.3.2) is the one re-entry into expression parsing that does not
// funnel through parseFactor, so MaxExprDepth/ExprDepth above -- which
// bounds every *other* recursive edge in the parseExpression/
// parseSimpleExpr/parseTerm/parsePower/parseFactor cycle -- never fires for
// it (issue #550).  Unlike that guard, and unlike TypeDepth/StmtDepth/
// BlockDepth elsewhere in this file's siblings, this is deliberately NOT a
// term-count ceiling: a first attempt at this fix (PR #553, reverted; see
// issue #300's reopening comment) reused MaxExprDepth's own constant for an
// unrelated, iteratively-folded flat-chain loop and rejected input `main`
// had always accepted.  A dedicated term-count constant for *this*
// genuinely-recursive edge would dodge that specific mistake, but would
// still face a version of the same underlying problem: the "right" count
// isn't a fixed number, it's however many parsePower frames actually fit in
// the stack space available, which depends on how large one parsePower
// frame is -- itself a function of build type (Debug frames are
// substantially larger than Release's optimized ones) and platform, neither
// of which a compile-time constant can track.
//
// So this measures real stack headroom directly instead, the same way
// clang::Sema::isStackNearlyExhausted() does (clang/Basic/Stack.h, not
// linked into plang but a design precedent): llvm::getStackPointer() and
// llvm::getDefaultStackSize() (llvm/Support/ProgramStack.h) are both already
// reusable utilities LLVM ships and this project already links against
// (confirmed: getDefaultStackSize() is backed by getrlimit(RLIMIT_STACK) on
// POSIX, with its own platform-appropriate fallback when that is
// unavailable or unlimited, so this tracks the actual runtime stack budget
// rather than guessing at one).  A 1000-term '**' chain -- Sema's own
// MaxExprDepth (Sema.h), the deepest expression Sema itself ever accepts --
// costs on the order of a few hundred KB of stack even under an
// unoptimized Debug build, nowhere near the several-MiB budget this checks
// against, so this never rejects anything Sema's own limit would have
// accepted anyway.
static bool powerStackNearlyExhausted(std::uintptr_t Baseline) {
    const std::uintptr_t Current = llvm::getStackPointer();
    // The stack grows down on every architecture plang targets (x86-64,
    // AArch64), and Baseline was captured at Parser construction, further up
    // an always-shallower stack than any point parsing itself can reach, so
    // Baseline >= Current holds once any recursion at all has happened; the
    // clamp below is just defensive in case some unusual environment
    // violates that (e.g. a split/segmented stack where the two addresses
    // are not directly comparable this way).
    const std::uintptr_t Used = (Baseline > Current) ? (Baseline - Current) : 0;
    const size_t Budget = llvm::getDefaultStackSize();
    // Scale the margin down for a small budget rather than treating
    // PowerStackSafetyMargin as an absolute floor: a budget at or below that
    // fixed 1 MiB (a small-but-real platform limit -- see
    // PowerStackSafetyMargin's own comment for concrete examples) must not
    // make every call here look "exhausted" independent of how much of the
    // budget is actually in use, which is what comparing against an
    // unscaled, possibly-larger-than-the-whole-budget margin did (a
    // trivial, zero-nesting `x ** y` was rejected outright under such a
    // budget -- found in PR #555's own review). Reserving a quarter of the
    // real budget, capped at the original 1 MiB, keeps a normal-sized
    // (multi-MiB) stack's behavior byte-for-byte identical to before while
    // still reserving genuine headroom -- comfortably more than one
    // parsePower frame ever costs -- to safely unwind and tear down the AST
    // once a small budget's own, correspondingly smaller ceiling is reached,
    // so a genuinely deep '**' chain is still caught before it actually
    // overflows the stack rather than the check being disabled outright.
    const size_t Margin = std::min<size_t>(PowerStackSafetyMargin, Budget / 4);
    return Used >= (Budget - Margin);
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
        // See powerStackNearlyExhausted's own comment above for why this
        // recursive edge -- unlike every other one in this file -- is
        // bounded by live stack headroom rather than a term-count ceiling.
        //
        // Unlike ExprDepthScope below (parseFactor), whose ceiling can only
        // ever fire once MaxExprDepth other DepthGuards are already alive on
        // the stack -- guaranteeing a Guard is already there to eventually
        // reset the "already reported" flag as those unwind -- this check is
        // driven by absolute stack headroom, so it can fire on the very
        // *first* call (PowerDepth == 0), before any Guard has ever been
        // constructed. So the Guard is constructed unconditionally here,
        // before the check, rather than only in the not-exhausted branch
        // below: otherwise a first-call exhaustion would set
        // PowerDepthLimitHit with no Guard ever created to reset it,
        // latching the flag true for the rest of this Parser's lifetime and
        // silently swallowing the diagnostic for a later, unrelated '**'
        // chain elsewhere in the same file (found in PR #555's own review).
        PowerDepthScope Guard(PowerDepth, PowerDepthLimitHit);
        if (powerStackNearlyExhausted(StackBaseline)) {
            if (!PowerDepthLimitHit) {
                PowerDepthLimitHit = true;
                emitError(Current.toLoc(), diag::err_expr_too_deeply_nested);
            }
            // Unlike parseFactor's own ExprDepth ceiling -- where every
            // caller still on the stack is waiting on its own matching ')'
            // and unwinds cleanly once it sees one still there -- a '**'
            // chain has no such per-level closing token for an aborted
            // right operand to leave for an enclosing caller to find. Left
            // as a bare stub, the remainder of the chain (however many more
            // 'expop factor' pairs the adversarial input still has queued
            // up) would simply sit unconsumed and desync everything that
            // parses after this expression, cascading into a burst of
            // unrelated-looking "expected ';'"/"expected 'end'"/etc.
            // diagnostics -- still bounded and non-crashing, but needless
            // noise the "one diagnostic, not a pile of confusing ones"
            // precedent elsewhere in this file (see ExprDepthLimitHit's own
            // comment) already avoids for every other guard. So drain the
            // rest of the chain here instead: same 'expop factor' shape the
            // grammar comment above already describes, just iterative
            // rather than recursive, and its parsed operands are discarded
            // rather than linked into the tree -- this stub subtree is
            // already being reported as an error, so what it evaluates to
            // does not matter, only that the token stream ends up
            // positioned after the whole chain rather than in the middle
            // of it. Current is already sitting on the immediate right
            // operand (the operator itself was consumed above before this
            // check ever runs), so that one is parsed normally -- via
            // parseFactor, not a further parsePower recursion, which is the
            // whole point -- and becomes Node->Right for real; only any
            // *further* 'expop factor' pairs past it are drained and
            // discarded.
            Node->Right = parseFactor();
            while (isExpop(Current.Kind)) {
                advance();
                parseFactor();
            }
        } else {
            Node->Right = parsePower();   // right-associative
        }
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
//   '[' expression ']'        → IndexExpr
//   '.' identifier            → FieldExpr
//   '.' identifier '(' ... ')' → MethodCallExpr (Turbo Tier 5, Cluster A item 3)
//   '^'                        → DerefExpr
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
            std::string FieldName = expect(TokenKind::Identifier).Lexeme;
            // Turbo Tier 5, Cluster A item 3: '.identifier' immediately
            // followed by '(' is built as a MethodCallExpr rather than a
            // FieldExpr -- see MethodCallExpr's own comment (AstExpr.h) for
            // why the parser cannot (and does not need to) tell a genuine
            // method call apart from anything else here; Sema decides.
            // Reuses the plain parseExpression-list argument-parsing shape
            // (not parseSizeHighLowArg's SizeOf/High/Low special case,
            // which applies only to those three builtins' bare identifier
            // call form just below in parseFactor).
            if (check(TokenKind::LeftParen)) {
                advance(); // consume '('
                auto Node      = std::make_unique<MethodCallExpr>();
                Node->Loc      = Loc;
                Node->Receiver = std::move(Expr);
                Node->Method   = FieldName;
                if (!check(TokenKind::RightParen)) {
                    Node->Args.push_back(parseExpression());
                    while (match(TokenKind::Comma)) {
                        Node->Args.push_back(parseExpression());
                    }
                }
                expect(TokenKind::RightParen);
                Expr = std::move(Node);
            } else {
                auto Node    = std::make_unique<FieldExpr>();
                Node->Loc    = Loc;
                Node->Record = std::move(Expr);
                Node->Field  = FieldName;
                Expr = std::move(Node);
            }
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
    // Every recursive re-entry into expression parsing -- '(' below, which
    // calls back into parseExpression, and 'not', which calls parseFactor
    // directly -- funnels through this activation, so this is the one place
    // a ceiling bounds the whole cycle.  Checked before the RAII bump a few
    // lines down: a caller already sitting at the ceiling must return without
    // recursing again, not recurse once more and only then stop.
    if (ExprDepth >= MaxExprDepth) {
        if (!ExprDepthLimitHit) {
            ExprDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_expr_too_deeply_nested);
        }
        // Deliberately does not consume Current (typically another '(') --
        // every caller up the stack is still waiting on a matching ')' and
        // unwinds on its own once it sees the same token still there.
        auto Node   = std::make_unique<IntLitExpr>();
        Node->Loc   = Current;
        Node->Value = 0;
        return Node;
    }
    ExprDepthScope DepthGuard(ExprDepth, ExprDepthLimitHit);

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

        // Turbo prefix address-of: '@' factor.  The scanner (Scanner.cpp's
        // '@' dispatch) is the sole dialect gate -- it only ever hands back
        // an At token under -std=turbo, so this case is unreachable under
        // ISO 7185/EP the same way an EP-only keyword token would be.
        // Recursing into parseFactor (not a narrower production) lets '@'
        // stack with postfix operators the way 'not' does above: '@x.f'
        // reads as the address of field f, '@x^' as the address of what x
        // points to, matching how tightly '.'/'^'/'[' already bind.
        case TokenKind::At: {
            auto Node     = std::make_unique<UnaryExpr>();
            Node->Loc     = Loc;
            Node->Op      = TokenKind::At;
            advance();
            Node->Operand = parseFactor();
            return Node;
        }

        case TokenKind::LeftParen: {
            advance();
            auto Expr = parseExpression();
            // Suppressed while unwinding from the depth ceiling above: every
            // stacked '(' between here and the ceiling is missing its ')' for
            // the same reason, and expect()'s diagnostic once per level would
            // bury the one diagnostic that actually explains the failure.
            if (ExprDepthLimitHit)
                match(TokenKind::RightParen);
            else
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

        // Turbo VALUE typecast using one of the five standard type names that
        // are lexer KEYWORDS rather than Identifier tokens (TokenKinds.def),
        // so the 'case TokenKind::Identifier' arm below never sees them:
        // Integer(x), Real(x), Boolean(x), Char(x), String(x).  Gated on
        // Opts.turbo() -- ISO 7185/Extended Pascal have no typecast syntax at
        // all, and a bare occurrence of one of these keywords means nothing
        // there either, so the fallback below reproduces the exact
        // "expected expression" diagnostic and recovery this position has
        // always had for them.
        case TokenKind::Integer:
        case TokenKind::Real:
        case TokenKind::Boolean:
        case TokenKind::Char:
        case TokenKind::String: {
            const TokenKind KeywordKind = Current.Kind;
            advance();
            if (Opts.turbo() && check(TokenKind::LeftParen)) {
                advance(); // consume '('
                auto Node      = std::make_unique<TypeCastExpr>();
                Node->Loc      = Loc;
                Node->TypeName = Loc.Lexeme;
                Node->Operand  = parseExpression();
                expect(TokenKind::RightParen);
                return parsePostfix(std::move(Node));
            }
            emitError(Loc.toLoc(), diag::err_expected_expr, {describe(KeywordKind)});
            auto Node   = std::make_unique<IntLitExpr>();
            Node->Loc   = Loc;
            Node->Value = 0;
            return Node;
        }

        // Turbo Tier 5, issue #509: 'inherited;' (bare) / 'inherited Method'
        // / 'inherited Method(args)' used as a VALUE -- the
        // expression-context sibling of Parser::parseStatement's identical
        // TokenKind::Inherited case (ParseStmt.cpp), which builds an
        // InheritedCallStmt the same way this builds an InheritedCallExpr;
        // see InheritedCallExpr's own comment (AstExpr.h) for why these are
        // two node kinds rather than one, the same MethodCallExpr/
        // MethodCallStmt split already established just above.  'inherited'
        // is a DIALECT_KEYWORD (TokenKinds.def) reserved only under Turbo,
        // so this case is reached at all only there -- see ParseStmt.cpp's
        // own identical comment for why no separate Opts.turbo() check is
        // needed here either.  Sema (Sema::checkInheritedCallExpr) is what
        // actually confirms this appears inside a method body and resolves
        // Method against the enclosing method's own OwnerType's ancestor
        // chain; the parser only knows the token shape.
        case TokenKind::Inherited: {
            advance();
            auto Node = std::make_unique<InheritedCallExpr>();
            Node->Loc = Loc;
            if (check(TokenKind::Identifier)) {
                Node->Method = Current.Lexeme;
                advance();
                if (match(TokenKind::LeftParen)) {
                    if (!check(TokenKind::RightParen)) {
                        Node->Args.push_back(parseExpression());
                        while (match(TokenKind::Comma))
                            Node->Args.push_back(parseExpression());
                    }
                    expect(TokenKind::RightParen);
                }
            }
            // Postfix chaining ('.Field', '[i]', '^') applies to an
            // 'inherited' call's own result exactly as it does to an
            // ordinary CallExpr just below -- e.g. a function-valued
            // 'inherited' result feeding straight into a further field or
            // index access.
            return parsePostfix(std::move(Node));
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

            // Turbo VALUE typecast: TypeName(expr).  Recognized here, not
            // deferred to Sema the way an ordinary call is, because the node
            // this builds has to be a TypeCastExpr rather than a CallExpr --
            // see TypeCastExpr's own comment (AstExpr.h) for why the lvalue
            // form needs a distinct NodeKind.  A type name and a routine name
            // can never share a scope in Pascal, so TypeNames_ is a sound way
            // to decide which of the two grammars '(' begins; VarNames_
            // breaks the tie in favor of a variable that shadows an outer
            // type, the same precedence parseStructuredValueOrIndex already
            // gives EP's TypeName[...] just below.
            {
                const std::string Lower = toLower(Name);
                const bool NamesAType = Opts.turbo() && TypeNames_.count(Lower)
                                         && !VarNames_.count(Lower);
                if (NamesAType && check(TokenKind::LeftParen)) {
                    advance(); // consume '('
                    auto Node      = std::make_unique<TypeCastExpr>();
                    Node->Loc      = Loc;
                    Node->TypeName = Name;
                    Node->Operand  = parseExpression();
                    expect(TokenKind::RightParen);
                    return parsePostfix(std::move(Node));
                }
            }

            if (match(TokenKind::LeftParen)) {
                auto Node  = std::make_unique<CallExpr>();
                Node->Loc  = Loc;
                Node->Name = Name;
                if (!check(TokenKind::RightParen)) {
                    // SizeOf/High/Low's FIRST argument only -- see
                    // parseSizeHighLowArg's own comment; every other
                    // argument to every other call keeps the ordinary
                    // parseExpression() production.
                    Node->Args.push_back(parseSizeHighLowArg(Name));
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

// case-constant → ('+' | '-')? factor.  See the declaration in Parser.h: a
// case-statement's label and a variant-part's are both ISO Sec6.3's signed
// `constant`, and parseFactor alone has no sign production, so a bare
// parseFactor() call for either rejected `-1` outright.  Structurally the
// same as parseSubrangeBound's own unconditional sign-handling branch
// (ParseType.cpp), kept separate because that one function is also the
// EP-relaxed-bounds entry point (guarded on SubrangeBoundExprs) and a
// case-constant is never that -- it is always exactly a signed factor,
// under every dialect.
std::unique_ptr<ExprNode> Parser::parseCaseConstant() {
    const Token Loc = Current;
    if (check(TokenKind::Plus)) { advance(); return parseFactor(); }
    if (check(TokenKind::Minus)) {
        advance();
        auto Node     = std::make_unique<UnaryExpr>();
        Node->Loc     = Loc;
        Node->Op      = TokenKind::Minus;
        Node->Operand = parseFactor();
        return Node;
    }
    return parseFactor();
}

// See the declaration (Parser.h) for why this exists at all.  Only the five
// primitive type-name KEYWORDS need a special production here: every other
// type name -- Byte, Word, a user's own TMyRecord, ... -- is an ordinary
// identifier, already parsed by the IdentExpr branch just above this call
// site exactly like a variable reference would be, with the "is it really a
// variable or really a type" question left to Sema (checkCallExpr's own
// SizeOf/High/Low arm), not decided here.
std::unique_ptr<ExprNode> Parser::parseSizeHighLowArg(const std::string& Callee) {
    const std::string Lo = toLower(Callee);
    if (Lo == "sizeof" || Lo == "high" || Lo == "low") {
        switch (Current.Kind) {
        case TokenKind::Integer:
        case TokenKind::Real:
        case TokenKind::Boolean:
        case TokenKind::Char:
        case TokenKind::String: {
            auto Node   = std::make_unique<IdentExpr>();
            Node->Loc   = Current;
            Node->Name  = Current.Lexeme;
            advance();
            return Node;
        }
        default:
            break;
        }
    }
    return parseExpression();
}
