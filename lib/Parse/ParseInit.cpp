//===- ParseInit.cpp - Parsing of initial-state specifiers and structured values (EP §6.6). ===//

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

// Ceiling on live parseComponentValue and parseVariantPartValue activations
// together (Parser::ValueDepth).  Mirrors MaxExprDepth in ParseExpr.cpp: 500
// levels of nesting is nowhere near exhausting an 8MB default stack -- both
// recursive shapes EP §6.8.7's structured-value-constructor grammar allows,
// an arm value nested arbitrarily deep ('value [1:[1: ... :0]]') and a
// variant part nested arbitrarily deep ('value [case 1 of [case 1 of
// [...]]]'), crash only tens of thousands of levels deeper than this on this
// machine -- while no legitimate Extended Pascal program nests a structured
// value anywhere close to this deep.  Without a ceiling here, a source file
// built specifically to nest deeply (or a generated/fuzzed one) drives the
// real call stack instead of a diagnostic.
static constexpr unsigned MaxValueDepth = 500;

// ---------------------------------------------------------------------------
// Type expression parsing
// ---------------------------------------------------------------------------

// EP §6.6: the initial-state-specifier that may end a type-denoter.  It is
// read where a denoter ends — a type-definition, a variable-declaration, a
// record-section — and not inside parseTypeExpr, because §6.6 note 3 gives it
// to the whole denoter rather than to the component type it stands beside:
// `array [1..8] of char value [1..8: '*']` says what the array starts as, and
// `array [1..8] of char value '*'` is a violation rather than eight stars.
void Parser::parseInitialState(TypeNode& Node) {
    if (!Opts.extendedPascal() || !match(TokenKind::Value)) return;
    Node.InitialState = parseComponentValue();
}

// EP §6.8.7.1: component-value = expression | array-value | record-value.
// The two structured forms are written without a type name, the type being
// the one the place they appear in calls for.  A bracket may begin any of the
// three, a set-constructor being an expression, and what tells them apart is
// the colon that every arm of a structured value has.
std::unique_ptr<ExprNode> Parser::parseComponentValue() {
    // Every recursive re-entry through an arm's value -- 'value [1:[1: ...
    // :0]]' -- funnels straight back through this activation, so this is one
    // of the two places (see parseVariantPartValue below for the other) a
    // ceiling bounds the whole EP §6.8.7 structured-value-constructor cycle.
    // Checked before the RAII bump a few lines down: a caller already
    // sitting at the ceiling must return without recursing again, not
    // recurse once more and only then stop.
    if (ValueDepth >= MaxValueDepth) {
        if (!ValueDepthLimitHit) {
            ValueDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_value_too_deeply_nested);
        }
        // Deliberately does not consume Current (typically another '[') --
        // every caller up the stack is still waiting on its own closing
        // token (']', ';', etc.) and unwinds on its own once it sees the
        // same token still there.
        auto Node   = std::make_unique<IntLitExpr>();
        Node->Loc   = Current;
        Node->Value = 0;
        return Node;
    }
    ValueDepthScope DepthGuard(ValueDepth, ValueDepthLimitHit);

    if (!check(TokenKind::LeftBracket)) return parseExpression();
    Token Loc = Current;
    advance(); // '['

    auto structured = [&] {
        auto Node = std::make_unique<StructuredValueExpr>();
        Node->Loc = Loc;
        return Node;
    };

    // '[]' is the empty value of whatever type is called for.
    if (match(TokenKind::RightBracket)) return structured();

    if (check(TokenKind::Case) || check(TokenKind::Otherwise)) {
        auto Node = structured();
        parseValueArms(*Node);
        // Suppressed while unwinding from the depth ceiling above: every
        // enclosing '[' between here and the ceiling is missing its ']' for
        // the same reason, and expect()'s diagnostic once per level would
        // bury the one diagnostic that actually explains the failure.
        if (ValueDepthLimitHit)
            match(TokenKind::RightBracket);
        else
            expect(TokenKind::RightBracket);
        return Node;
    }

    // Both a set-constructor's elements and an arm's labels are a
    // comma-separated list of values and ranges, so the list is read first
    // and the token after it says which was written.
    std::vector<std::unique_ptr<ExprNode>> First;
    parseValueLabels(First);

    if (!check(TokenKind::Colon)) {
        auto Set      = std::make_unique<SetLiteralExpr>();
        Set->Loc      = Loc;
        Set->Elements = std::move(First);
        expect(TokenKind::RightBracket);
        return Set;
    }

    advance(); // ':'
    auto Node = structured();
    StructuredValueArm Arm;
    Arm.Labels = std::move(First);
    Arm.Value  = parseComponentValue();
    Node->Arms.push_back(std::move(Arm));
    if (match(TokenKind::Semicolon)) parseValueArms(*Node);
    // Suppressed while unwinding from the depth ceiling above: every
    // enclosing '[' between here and the ceiling is missing its ']' for the
    // same reason, and expect()'s diagnostic once per level would bury the
    // one diagnostic that actually explains the failure.
    if (ValueDepthLimitHit)
        match(TokenKind::RightBracket);
    else
        expect(TokenKind::RightBracket);
    return Node;
}

// One label, or one set element: a value, or a range written lo..hi.
void Parser::parseValueLabels(std::vector<std::unique_ptr<ExprNode>>& Out) {
    do {
        auto E = parseExpression();
        if (!match(TokenKind::DotDot)) { Out.push_back(std::move(E)); continue; }
        auto Rng  = std::make_unique<SetRangeExpr>();
        Rng->Loc  = E->Loc;
        Rng->Low  = std::move(E);
        Rng->High = parseExpression();
        Out.push_back(std::move(Rng));
    } while (match(TokenKind::Comma));
}

// The arms of an array-value or record-value: `labels : value` in either
// case, with §6.8.7.2's 'otherwise' completer and §6.8.7.3's variant part.
void Parser::parseValueArms(StructuredValueExpr& Node) {
    while (!check(TokenKind::RightBracket) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Case)) {
            parseVariantPartValue(Node);
        } else if (check(TokenKind::Otherwise)) {
            advance();
            match(TokenKind::Colon); // EP §6.8.7.2 writes none; one reads well
            StructuredValueArm Arm;
            Arm.IsOtherwise = true;
            Arm.Value       = parseComponentValue();
            Node.Arms.push_back(std::move(Arm));
            match(TokenKind::Semicolon);
            break; // the completer is last
        } else {
            StructuredValueArm Arm;
            parseValueLabels(Arm.Labels);
            expect(TokenKind::Colon);
            Arm.Value = parseComponentValue();
            Node.Arms.push_back(std::move(Arm));
        }
        if (!match(TokenKind::Semicolon)) break;
    }
}

// ---------------------------------------------------------------------------
// EP §6.8.7: Structured value constructor or array index
// ---------------------------------------------------------------------------
//
// Called when parseFactor sees Identifier '[' in EP mode; the identifier
// has already been consumed and is passed as Name.  This method consumes the
// '[' and everything up to the matching ']'.
//
// Disambiguation inside the brackets:
//   - 'otherwise' keyword       → array constructor ('otherwise' arm)
//   - label-list ':' value      → array or record constructor arm
//   - expression-list (no ':')  → typed set literal  or  single-element array index
//
// For the single-element-no-colon case (e.g. arr[i]), we reconstruct an
// IndexExpr and call parsePostfix so that chained postfixes ([j], .f, ^) work.
//
void Parser::parseVariantPartValue(StructuredValueExpr& Node) {
    // Every recursive re-entry through a variant part's field-list -- 'value
    // [case 1 of [case 1 of [...]]]' -- funnels straight back through this
    // activation without ever passing through parseComponentValue again, so
    // this is the other of the two places (see parseComponentValue above) a
    // ceiling bounds the whole EP §6.8.7 structured-value-constructor cycle.
    // Checked before the RAII bump a few lines down, and before consuming
    // 'case': a caller already sitting at the ceiling must return without
    // recursing again, not recurse once more and only then stop.
    if (ValueDepth >= MaxValueDepth) {
        if (!ValueDepthLimitHit) {
            ValueDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_value_too_deeply_nested);
        }
        // Deliberately does not consume 'case' -- every caller up the stack
        // (parseValueArms, parseFieldListValue, parseStructuredValueOrIndex)
        // is looping on a semicolon or unwinding toward its own closing ']'
        // and stops cleanly once it sees the same 'case' still sitting there
        // unconsumed, the same way the other guards above leave their own
        // lookahead token untouched.
        return;
    }
    ValueDepthScope DepthGuard(ValueDepth, ValueDepthLimitHit);

    advance(); // 'case'

    // The tag-field-identifier is optional, and what follows it looks the same
    // as the constant-tag-value that may stand alone, so read one and let the
    // colon say which it was.
    auto First = parseExpression();
    std::unique_ptr<ExprNode> TagField;
    std::unique_ptr<ExprNode> TagValue;
    if (match(TokenKind::Colon)) {
        TagField = std::move(First);
        TagValue = parseExpression();
    } else {
        TagValue = std::move(First);
    }
    expect(TokenKind::Of);
    expect(TokenKind::LeftBracket);

    // The selector is a component like any other when the variant part names a
    // tag field; where it does not, the value only chooses the variant.
    if (TagField) {
        StructuredValueArm Arm;
        Arm.Labels.push_back(std::move(TagField));
        Arm.Value = std::move(TagValue);
        Node.Arms.push_back(std::move(Arm));
    }

    parseFieldListValue(Node);
    // Suppressed while unwinding from the depth ceiling above: every
    // enclosing 'case ... of [' between here and the ceiling is missing its
    // ']' for the same reason, and expect()'s diagnostic once per level
    // would bury the one diagnostic that actually explains the failure.
    if (ValueDepthLimitHit)
        match(TokenKind::RightBracket);
    else
        expect(TokenKind::RightBracket);
}

void Parser::parseFieldListValue(StructuredValueExpr& Node) {
    while (!check(TokenKind::RightBracket) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Case)) {
            parseVariantPartValue(Node);
        } else {
            StructuredValueArm Arm;
            Arm.Labels.push_back(parseExpression());
            while (match(TokenKind::Comma))
                Arm.Labels.push_back(parseExpression());
            expect(TokenKind::Colon);
            Arm.Value = parseExpression();
            Node.Arms.push_back(std::move(Arm));
        }
        if (!match(TokenKind::Semicolon)) break;
    }
}

std::unique_ptr<ExprNode>
Parser::parseStructuredValueOrIndex(std::string Name, Token Loc) {
    // Current is '['; consume it.
    advance();

    // Empty brackets: TypeName[] → empty typed set literal.
    if (match(TokenKind::RightBracket)) {
        auto Node      = std::make_unique<SetLiteralExpr>();
        Node->Loc      = Loc;
        Node->TypeName = Name;
        return Node;
    }

    // EP §6.8.7.3: a record value may be nothing but a variant part.
    if (check(TokenKind::Case)) {
        auto Node      = std::make_unique<StructuredValueExpr>();
        Node->Loc      = Loc;
        Node->TypeName = Name;
        parseVariantPartValue(*Node);
        match(TokenKind::Semicolon);
        // Suppressed while unwinding from the depth ceiling above: every
        // enclosing '[' between here and the ceiling is missing its ']' for
        // the same reason, and expect()'s diagnostic once per level would
        // bury the one diagnostic that actually explains the failure.
        if (ValueDepthLimitHit)
            match(TokenKind::RightBracket);
        else
            expect(TokenKind::RightBracket);
        return parsePostfix(std::move(Node));
    }

    // 'otherwise' at the very start → definitely an array constructor.
    if (check(TokenKind::Otherwise)) {
        advance(); // consume 'otherwise'
        // EP §6.8.7.2 writes the completer 'otherwise' component-value, with
        // no colon.  A colon reads naturally beside the arms it follows, so it
        // is tolerated as well.
        match(TokenKind::Colon);
        auto val = parseExpression();
        StructuredValueArm arm;
        arm.IsOtherwise = true;
        arm.Value = std::move(val);
        auto Node      = std::make_unique<StructuredValueExpr>();
        Node->Loc      = Loc;
        Node->TypeName = Name;
        Node->Arms.push_back(std::move(arm));
        match(TokenKind::Semicolon); // allow (and ignore) trailing ';'
        expect(TokenKind::RightBracket);
        return parsePostfix(std::move(Node));
    }

    // Helper: parse one label (possibly a range) and push onto a vector.
    // Returns true if a range (SetRangeExpr) was created.
    auto parseOneLabel = [&](std::vector<std::unique_ptr<ExprNode>>& out) -> bool {
        auto e = parseExpression();
        if (match(TokenKind::DotDot)) {
            auto rng  = std::make_unique<SetRangeExpr>();
            rng->Loc  = e->Loc;
            rng->Low  = std::move(e);
            rng->High = parseExpression();
            out.push_back(std::move(rng));
            return true;
        }
        out.push_back(std::move(e));
        return false;
    };

    // Parse the first label (or element).
    std::vector<std::unique_ptr<ExprNode>> labels;
    bool firstIsRange = parseOneLabel(labels);

    // Collect additional comma-separated labels/elements.
    while (match(TokenKind::Comma)) {
        parseOneLabel(labels);
    }

    // Check what follows the label list.
    if (match(TokenKind::Colon)) {
        // Constructor arm: labels ':' value { ';' arm }.
        // EP §6.8.7.1: component-value = expression | array-value |
        // record-value, and the two structured forms are written WITHOUT a
        // type name -- the type is the one the place they appear in calls
        // for.  This TypeName[...] form used parseExpression for every arm's
        // value, which cannot start a structured form at all (a leading '['
        // is always a set-constructor to parseExpression); `parseValueArms`,
        // reached through a var-declaration's `value` clause, always used
        // parseComponentValue for the identical grammar and had no such gap.
        // `Outer[a: [1: 10; 2: 20]; b: 100]` -- a bare array literal nested
        // inside a named record constructor -- failed with "expected ']',
        // got ':'": the '[' opening the nested array was read as the start
        // of a set, whose own grammar has no colon in it anywhere.
        auto val  = parseComponentValue();
        auto Node = std::make_unique<StructuredValueExpr>();
        Node->Loc      = Loc;
        Node->TypeName = Name;

        StructuredValueArm firstArm;
        for (auto& lbl : labels)
            firstArm.Labels.push_back(std::move(lbl));
        firstArm.Value = std::move(val);
        Node->Arms.push_back(std::move(firstArm));

        while (match(TokenKind::Semicolon)) {
            if (check(TokenKind::RightBracket) || check(TokenKind::Eof))
                break; // trailing semicolon
            if (check(TokenKind::Otherwise)) {
                advance(); // 'otherwise'
                match(TokenKind::Colon);   // EP §6.8.7.2: the colon is optional
                auto otherwiseVal = parseComponentValue();
                StructuredValueArm othArm;
                othArm.IsOtherwise = true;
                othArm.Value = std::move(otherwiseVal);
                Node->Arms.push_back(std::move(othArm));
                match(TokenKind::Semicolon); // allow trailing ';' after otherwise
                break; // 'otherwise' must be last
            }
            // EP §6.8.7.3: a variant part follows the fixed part.
            if (check(TokenKind::Case)) {
                parseVariantPartValue(*Node);
                continue;
            }
            // Next arm
            StructuredValueArm nextArm;
            parseOneLabel(nextArm.Labels);
            while (match(TokenKind::Comma))
                parseOneLabel(nextArm.Labels);
            expect(TokenKind::Colon);
            nextArm.Value = parseComponentValue();
            Node->Arms.push_back(std::move(nextArm));
        }

        // Suppressed while unwinding from the depth ceiling above: every
        // enclosing '[' between here and the ceiling is missing its ']' for
        // the same reason, and expect()'s diagnostic once per level would
        // bury the one diagnostic that actually explains the failure.
        if (ValueDepthLimitHit)
            match(TokenKind::RightBracket);
        else
            expect(TokenKind::RightBracket);
        return parsePostfix(std::move(Node));
    }

    if (match(TokenKind::RightBracket)) {
        // No colon: decide among array index, substring, or typed set literal.
        // ISO §6.5.3.2: a[i, j] abbreviates a[i][j].  Only a type name can
        // begin a structured value, so anything else subscripts a variable,
        // however many subscripts there are.
        // A typed set constructor is written with a TYPE name; anything else
        // subscripts a variable.  A name that is both -- a variable shadowing
        // a type, ordinary ISO 7185 -- is the variable here, since that is the
        // reading in which the brackets can mean what they say.
        const std::string Lower = toLower(Name);
        const bool NamesAType = TypeNames_.count(Lower) && !VarNames_.count(Lower);
        if (labels.size() > 1 && !NamesAType) {
            std::unique_ptr<ExprNode> Expr = std::make_unique<IdentExpr>();
            Expr->Loc = Loc;
            static_cast<IdentExpr*>(Expr.get())->Name = Name;
            for (auto& lbl : labels) {
                auto Node   = std::make_unique<IndexExpr>();
                Node->Loc   = Loc;
                Node->Array = std::move(Expr);
                Node->Index = std::move(lbl);
                Expr = std::move(Node);
            }
            return parsePostfix(std::move(Expr));
        }
        // EP §6.8.7: a typed set constructor with exactly ONE element or one
        // range is still a typed set constructor.  These two arms took it as a
        // subscript and as a substring, so `cs['a']` and `cs['a'..'c']` were
        // rejected with "type name 'cs' cannot be used as a value" -- Sema
        // knowing exactly what was wrong and unable to do anything about the
        // shape the parser had already built.
        if (labels.size() == 1 && NamesAType) {
            auto Node      = std::make_unique<SetLiteralExpr>();
            Node->Loc      = Loc;
            Node->TypeName = Name;
            Node->Elements.push_back(std::move(labels[0]));
            return Node;
        }
        if (labels.size() == 1 && !firstIsRange) {
            // Single non-range item → plain array index (e.g. arr[i]).
            auto ident  = std::make_unique<IdentExpr>();
            ident->Loc  = Loc;
            ident->Name = Name;
            auto idx    = std::make_unique<IndexExpr>();
            idx->Loc    = Loc;
            idx->Array  = std::move(ident);
            idx->Index  = std::move(labels[0]);
            return parsePostfix(std::move(idx));
        }
        if (labels.size() == 1 && firstIsRange) {
            // Single range: preserve old parsePostfix behavior — this is a
            // substring variable s[lo..hi] (EP §6.5.6).
            // (Sema rejects it if the identifier is not a string(N) variable.)
            auto* rng  = llvm::dyn_cast<SetRangeExpr>(labels[0].get());
            auto ident = std::make_unique<IdentExpr>();
            ident->Loc = Loc;
            ident->Name = Name;
            auto sub   = std::make_unique<SubstringExpr>();
            sub->Loc   = Loc;
            sub->Str   = std::move(ident);
            sub->Low   = std::move(rng->Low);
            sub->High  = std::move(rng->High);
            return sub;
        }
        // Multiple items (or multiple ranges) → typed set literal.
        auto Node      = std::make_unique<SetLiteralExpr>();
        Node->Loc      = Loc;
        Node->TypeName = Name;
        for (auto& lbl : labels)
            Node->Elements.push_back(std::move(lbl));
        return Node;
    }

    // Unexpected token — emit an error and recover.
    emitError(Current.toLoc(), diag::err_expected_token,
              {describe(TokenKind::RightBracket), describe(Current.Kind)});
    auto ident  = std::make_unique<IdentExpr>();
    ident->Loc  = Loc;
    ident->Name = Name;
    return ident;
}
