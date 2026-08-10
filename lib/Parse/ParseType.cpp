//===- ParseType.cpp - Parsing of type denoters (§6.4). ===//

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

std::unique_ptr<TypeNode> Parser::parseTypeExpr() {
    Token Loc = Current;

    // EP §6.4.1: 'bindable' qualifies the denoter without changing the type it
    // denotes.  It is what makes a variable of the type one that bind and
    // unbind will accept, so the flag rides on the node.
    if (Opts.extendedPascal() && check(TokenKind::Bindable)) {
        advance();
        auto Inner = parseTypeExpr();
        if (Inner) Inner->Bindable = true;
        return Inner;
    }

    // 'packed' is a modifier that can precede array, record, or set.
    // When it appears before one of those keywords, the packed flag is threaded
    // into the specific node.  When it appears before anything else (unusual but
    // legal in some dialects) we wrap with PackedTypeNode.
    if (match(TokenKind::Packed))
        return parsePackedTypeTail(Loc, /*ConformantAllowed=*/false);

    // EP §6.4.2.5: 'restricted T' denotes a type of its own, whose values are
    // those of T with every operation but parameter passing taken away.  What
    // follows is a type name, never a new type, so it is read here rather than
    // by recursing.
    if (Opts.extendedPascal() && check(TokenKind::Restricted)) {
        advance();
        auto Node  = std::make_unique<NamedTypeNode>();
        Node->Loc  = Loc;
        Node->Restricted = true;
        switch (Current.Kind) {
            case TokenKind::Identifier: case TokenKind::Integer:
            case TokenKind::Real:       case TokenKind::Boolean:
            case TokenKind::Char:       case TokenKind::String:
                Node->Name = Current.Lexeme;
                advance();
                break;
            default:
                emitError(Current.toLoc(),
                          "'restricted' must be followed by the name of a type");
                Node->Name = "integer";
                break;
        }
        return Node;
    }

    switch (Current.Kind) {
        // Built-in type keywords → NamedTypeNode.
        case TokenKind::Integer:
        case TokenKind::Real:
        case TokenKind::Boolean:
        case TokenKind::Char: {
            auto Node  = std::make_unique<NamedTypeNode>();
            Node->Loc  = Loc;
            Node->Name = Current.Lexeme;
            advance();
            return Node;
        }

        // EP §6.4.3.3: string(N) — variable-length string type.
        // Plain 'string' without '(' remains a NamedTypeNode for ISO 7185 compat.
        case TokenKind::String: {
            advance();
            if (match(TokenKind::LeftParen)) {
                auto Node      = std::make_unique<StringTypeNode>();
                Node->Loc      = Loc;
                Node->Capacity = parseSimpleExpr();
                expect(TokenKind::RightParen);
                return Node;
            }
            auto Node  = std::make_unique<NamedTypeNode>();
            Node->Loc  = Loc;
            Node->Name = "string";
            return Node;
        }

        // User-defined type name — may also be the lower bound of a subrange.
        case TokenKind::Identifier: {
            std::string Name = Current.Lexeme;
            advance();
            if (check(TokenKind::DotDot)) {
                // subrange: name '..' high
                advance();
                auto Low   = std::make_unique<IdentExpr>();
                Low->Loc   = Loc;
                Low->Name  = Name;
                auto Node  = std::make_unique<SubrangeTypeNode>();
                Node->Loc  = Loc;
                Node->Low  = std::move(Low);
                Node->High = parseSubrangeBound();
                return Node;
            }
            // EP §6.4.8: schema instantiation — SchemaName '(' actual-list ')'
            if (Opts.extendedPascal() && check(TokenKind::LeftParen)) {
                auto STNode   = std::make_unique<SchemaTypeNode>();
                STNode->Loc   = Loc;
                STNode->Name  = Name;
                advance(); // consume '('
                STNode->Actuals.push_back(parseSimpleExpr());
                while (match(TokenKind::Comma))
                    STNode->Actuals.push_back(parseSimpleExpr());
                expect(TokenKind::RightParen);
                return STNode;
            }
            auto Node  = std::make_unique<NamedTypeNode>();
            Node->Loc  = Loc;
            Node->Name = Name;
            return Node;
        }

        // Literals and signed constants — only valid as subrange lower bounds.
        // ISO §6.4.2.4 allows a sign on either bound, so `type r = -1..10` is a
        // subrange and not a malformed type name.
        case TokenKind::IntLit:
        case TokenKind::StringLit:
        case TokenKind::True:
        case TokenKind::False:
        case TokenKind::Minus:
        case TokenKind::Plus: {
            auto Low  = parseSubrangeBound();
            expect(TokenKind::DotDot);
            auto Node  = std::make_unique<SubrangeTypeNode>();
            Node->Loc  = Loc;
            Node->Low  = std::move(Low);
            Node->High = parseSubrangeBound();
            return Node;
        }

        case TokenKind::Array:
            return parseArrayType(false);

        case TokenKind::Record:
            return parseRecordType(false);

        case TokenKind::Set: {
            advance();
            expect(TokenKind::Of);
            auto Node    = std::make_unique<SetTypeNode>();
            Node->Loc    = Loc;
            Node->Base   = parseTypeExpr();
            Node->Packed = false;
            return Node;
        }

        case TokenKind::File: {
            advance();
            auto Node = std::make_unique<FileTypeNode>();
            Node->Loc = Loc;
            // EP §6.4.3.6: optional '[' index-type ']' for direct-access files
            if (Opts.extendedPascal() && check(TokenKind::LeftBracket)) {
                advance(); // consume '['
                Node->Index = parseTypeExpr();
                expect(TokenKind::RightBracket);
            }
            if (match(TokenKind::Of)) {
                Node->Element = parseTypeExpr();
            }
            return Node;
        }

        case TokenKind::Caret: {
            // pointer-type → '^' type-expr
            advance();
            auto Node  = std::make_unique<PointerTypeNode>();
            Node->Loc  = Loc;
            Node->Base = parseTypeExpr();
            return Node;
        }

        case TokenKind::LeftParen: {
            // enum-type → '(' identifier (',' identifier)* ')'
            advance();
            auto Node = std::make_unique<EnumTypeNode>();
            Node->Loc = Loc;
            Node->Values.push_back(expect(TokenKind::Identifier).Lexeme);
            while (match(TokenKind::Comma)) {
                Node->Values.push_back(expect(TokenKind::Identifier).Lexeme);
            }
            expect(TokenKind::RightParen);
            return Node;
        }

        // EP §6.4.9: type-inquiry = 'type' 'of' variable-access
        // 'type' here introduces a type-inquiry; in ISO 7185 mode 'type' only
        // appears as a section header (handled by parseBlock), never in a type-expr.
        case TokenKind::Type: {
            if (!Opts.extendedPascal()) {
                emitError(Loc.toLoc(), diag::err_expected_type_expr, {describe(Current.Kind)});
                advance();
                auto Node  = std::make_unique<NamedTypeNode>();
                Node->Loc  = Loc;
                Node->Name = "<error>";
                return Node;
            }
            advance(); // consume 'type'
            expect(TokenKind::Of);
            auto Node    = std::make_unique<TypeOfNode>();
            Node->Loc    = Loc;
            Node->VarName = expect(TokenKind::Identifier).Lexeme;
            return Node;
        }

        default: {
            emitError(Loc.toLoc(), diag::err_expected_type_expr, {describe(Current.Kind)});
            advance(); // consume unknown token to prevent loops
            auto Node  = std::make_unique<NamedTypeNode>();
            Node->Loc  = Loc;
            Node->Name = "<error>";
            return Node;
        }
    }
}

// array-type → 'array' '[' expr '..' expr ']' 'of' type-expr
/// One bound of a subrange type.  ISO §6.4.2.4 defines a bound as a constant,
/// which may carry a sign; EP §6.4.2.4 widens it to a general constant
/// expression, and parseSimpleExpr already accepts a leading sign there.
std::unique_ptr<ExprNode> Parser::parseSubrangeBound() {
    if (Opts.extendedPascal()) return parseSimpleExpr();

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

// An index is an ordinal type denoter (ISO §6.4.3.2): a range in nearly every
// array ever written, but equally a type name, `boolean`, or an enumeration
// spelled out in place.  This is not parseTypeExpr because the bounds of an
// array index are full constant expressions — `array[1..k*2]` — while those of
// a subrange type-denoter are narrower.
std::unique_ptr<TypeNode> Parser::parseArrayIndexType() {
    const Token Loc = Current;

    // Denoters that cannot begin an expression, so no range can be meant.
    switch (Current.Kind) {
    case TokenKind::LeftParen:  // enumeration written in place
    case TokenKind::Boolean:
    case TokenKind::Char:
    case TokenKind::Integer:
    // real and string are not ordinal types and cannot index anything, but
    // going through parseTypeExpr is what gets them the diagnostic that says
    // so rather than one about an expression that would not parse.
    case TokenKind::Real:
    case TokenKind::String:
        return parseTypeExpr();
    default:
        break;
    }

    // Otherwise parse an expression and let what follows decide: `..` makes it
    // the lower bound of a range, and anything else means the index was named
    // by its type, which reads as a bare identifier.
    auto First = parseExpression();
    if (match(TokenKind::DotDot)) {
        auto Node  = std::make_unique<SubrangeTypeNode>();
        Node->Loc  = Loc;
        Node->Low  = std::move(First);
        Node->High = parseExpression();
        return Node;
    }
    if (auto* Id = llvm::dyn_cast<IdentExpr>(First.get())) {
        auto Node  = std::make_unique<NamedTypeNode>();
        Node->Loc  = Loc;
        Node->Name = Id->Name;
        return Node;
    }
    emitError(Loc.toLoc(), diag::err_expected_type_expr,
              {describe(Current.Kind)});
    auto Node  = std::make_unique<NamedTypeNode>();
    Node->Loc  = Loc;
    Node->Name = "<error>";
    return Node;
}

std::unique_ptr<ArrayTypeNode> Parser::parseArrayType(bool Packed) {
    const Token Loc = Current;
    expect(TokenKind::Array);
    expect(TokenKind::LeftBracket);

    std::vector<std::unique_ptr<TypeNode>> Indices;
    do {
        Indices.push_back(parseArrayIndexType());
    } while (match(TokenKind::Comma));

    expect(TokenKind::RightBracket);
    expect(TokenKind::Of);

    // ISO §6.4.3.2: array[i1, i2] of T abbreviates array[i1] of array[i2] of T,
    // so the nesting is built from the innermost index outwards.
    std::unique_ptr<TypeNode> Inner = parseTypeExpr();
    for (auto It = Indices.rbegin(); It != Indices.rend(); ++It) {
        auto Node     = std::make_unique<ArrayTypeNode>();
        Node->Loc     = Loc;
        Node->Packed  = Packed;
        Node->Element = std::move(Inner);
        // A subrange is unwrapped into the bounds an array index has always
        // been held as, so that this produces exactly the tree it used to for
        // every array whose index is a range.
        if (auto* Sub = llvm::dyn_cast<SubrangeTypeNode>(It->get())) {
            Node->Low  = std::move(Sub->Low);
            Node->High = std::move(Sub->High);
        } else {
            Node->Index = std::move(*It);
        }
        Inner = std::move(Node);
    }
    // The loop runs at least once: parseTypeExpr always yields a node, so
    // Indices is never empty even when the index failed to parse.
    return std::unique_ptr<ArrayTypeNode>(
        llvm::cast<ArrayTypeNode>(Inner.release()));
}

// The rest of a packed type, 'packed' having been consumed at \p Loc.
//
// ConformantAllowed is set in a formal parameter list and nowhere else: ISO
// §6.6.3.7.1 admits `packed array [lo..hi: T] of E` there, which is the form a
// string of any length is passed as, and a packed array is an ordinary one
// everywhere else.
std::unique_ptr<TypeNode> Parser::parsePackedTypeTail(Token Loc,
                                                      bool ConformantAllowed) {
    if (check(TokenKind::Array))
        return ConformantAllowed ? parseConformantOrRegular(/*Packed=*/true)
                                 : parseArrayType(true);
    if (check(TokenKind::Record)) return parseRecordType(true);
    if (check(TokenKind::Set)) {
        advance();
        expect(TokenKind::Of);
        auto Node    = std::make_unique<SetTypeNode>();
        Node->Loc    = Loc;
        Node->Base   = parseTypeExpr();
        Node->Packed = true;
        return Node;
    }
    // Fallback: wrap whatever follows.
    auto Node   = std::make_unique<PackedTypeNode>();
    Node->Loc   = Loc;
    Node->Inner = parseTypeExpr();
    return Node;
}

// EP §6.7.3.7: parseConformantOrRegular
// Called from parseParamGroup when we see 'array' in EP mode.
// Consumes 'array', then '[', then reads one or two tokens to determine
// whether this is a conformant schema (identifier .. identifier :) or
// a regular array type (anything else).
//
// Conformant schema:     array [lo..hi : OrdType {; lo..hi : OrdType}] of ElemType
// Regular array type:    array [expr .. expr] of ElemType
//
// Multi-dimension abbreviated form expands to nested ConformantArrayTypeNode:
//   array [u..v:T1; j..k:T2] of E  →  outer{u..v:T1, element=inner{j..k:T2, element=E}}
std::unique_ptr<TypeNode> Parser::parseConformantOrRegular(bool Packed) {
    Token Loc = Current;
    expect(TokenKind::Array);
    expect(TokenKind::LeftBracket);

    // Look-ahead: try to parse the first index.
    // If we get: Identifier DotDot Identifier Colon  → conformant
    // Otherwise: regular array (we have to reconstruct from what we consumed)
    //
    // Strategy: parse the first "expression" speculatively.
    // For conformant syntax:   lo  ..  hi : OrdType
    // For regular syntax:      expr .. expr ]
    //
    // Both start the same way so we use this rule:
    //   - If current is an Identifier, save it.
    //   - Consume it and check if next is DotDot.
    //   - If yes, save it and consume. Now current should be hi Identifier.
    //   - If current is Identifier, save it and consume.
    //     - If next is Colon → conformant!
    //     - If next is RightBracket or Semicolon → regular (lo..hi uses two idents)
    //     - Otherwise → regular (hi is something weird; reconstruct)
    //   - If first token is not Identifier → regular array.

    if (check(TokenKind::Identifier)) {
        std::string loName = Current.Lexeme;
        Token loTok = Current;
        advance(); // consume lo

        if (check(TokenKind::DotDot)) {
            advance(); // consume '..'

            if (check(TokenKind::Identifier)) {
                std::string hiName = Current.Lexeme;
                Token hiTok = Current;
                advance(); // consume hi

                if (check(TokenKind::Colon)) {
                    // ---- Conformant array schema ----
                    // Parse all dimensions: lo..hi:T {; lo..hi:T}
                    std::vector<IndexSpec> specs;

                    // Helper: consume the current token as a type name (may be an
                    // Identifier or a built-in type keyword like 'integer', 'char').
                    auto consumeTypeName = [&]() -> std::string {
                        std::string name = Current.Lexeme;
                        if (name.empty()) name = std::string(spelling(Current.Kind));
                        advance();
                        return name;
                    };

                    // First dimension already parsed (lo, hi); now consume ':'
                    advance(); // consume ':'
                    std::string ordType = consumeTypeName();
                    specs.push_back({loName, hiName, ordType});

                    // Additional dimensions
                    while (match(TokenKind::Semicolon)) {
                        std::string lo2 = expect(TokenKind::Identifier).Lexeme;
                        expect(TokenKind::DotDot);
                        std::string hi2 = expect(TokenKind::Identifier).Lexeme;
                        expect(TokenKind::Colon);
                        std::string ot2 = consumeTypeName();
                        specs.push_back({lo2, hi2, ot2});
                    }
                    expect(TokenKind::RightBracket);
                    expect(TokenKind::Of);

                    // Parse element type.  In EP mode the element may itself be a
                    // conformant array schema (e.g. the nested dimension of a 2D conformant
                    // parameter).  Route through parseConformantOrRegular so it is parsed
                    // correctly.
                    std::unique_ptr<TypeNode> elemType;
                    if (check(TokenKind::Array)) {
                        elemType = parseConformantOrRegular(/*Packed=*/false);
                    } else if (check(TokenKind::Packed)) {
                        Token PLoc = Current;
                        advance();
                        elemType = parsePackedTypeTail(
                            PLoc, /*ConformantAllowed=*/true);
                    } else {
                        elemType = parseTypeExpr();
                    }

                    // ISO §6.6.3.7.1: a packed conformant array schema has one
                    // dimension and an element type written as a name.  The
                    // unpacked form is the one that nests.
                    if (Packed && specs.size() > 1)
                        emitError(Loc.toLoc(),
                                  diag::err_packed_conformant_shape, {});

                    // Build nested ConformantArrayTypeNode from the inside out.
                    // specs[last] wraps elemType, specs[last-1] wraps that, etc.
                    // Since we already have all specs, build from the innermost outward.
                    std::unique_ptr<TypeNode> result = std::move(elemType);
                    for (int i = (int)specs.size() - 1; i >= 0; --i) {
                        auto node       = std::make_unique<ConformantArrayTypeNode>();
                        node->Loc       = Loc;
                        node->Packed    = Packed;
                        node->Specs     = {specs[i]};
                        node->Element   = std::move(result);
                        result          = std::move(node);
                    }
                    return result;
                }

                // Not conformant (no colon after hi identifier).
                // Reconstruct as a regular array with lo..hi where lo and hi are
                // IdentExpr nodes.
                auto loExpr  = std::make_unique<IdentExpr>();
                loExpr->Loc  = loTok;
                loExpr->Name = loName;

                auto hiExpr  = std::make_unique<IdentExpr>();
                hiExpr->Loc  = hiTok;
                hiExpr->Name = hiName;

                // Hi might be followed by more expression tokens — but IdentExpr alone
                // could be a valid constant, so continue as regular array.
                auto node    = std::make_unique<ArrayTypeNode>();
                node->Loc    = Loc;
                node->Packed = Packed;
                node->Low    = std::move(loExpr);
                node->High   = std::move(hiExpr);
                expect(TokenKind::RightBracket);
                expect(TokenKind::Of);
                node->Element = parseTypeExpr();
                return node;
            }

            // hi is not an identifier — regular array, lo was an ident.
            // Reconstruct: lo is an IdentExpr, hi is parsed by parseExpression.
            auto loExpr  = std::make_unique<IdentExpr>();
            loExpr->Loc  = loTok;
            loExpr->Name = loName;

            auto node    = std::make_unique<ArrayTypeNode>();
            node->Loc    = Loc;
            node->Packed = Packed;
            node->Low    = std::move(loExpr);
            node->High   = parseExpression(); // parse remaining hi
            expect(TokenKind::RightBracket);
            expect(TokenKind::Of);
            node->Element = parseTypeExpr();
            return node;
        }

        // No '..' after lo identifier — regular array with lo being an IdentExpr.
        // Parse lo..hi normally but lo was already consumed.
        // This path is unusual (e.g. array [SomeType] is invalid anyway).
        // Just build a SubrangeTypeNode as low expression.
        auto loExpr  = std::make_unique<IdentExpr>();
        loExpr->Loc  = loTok;
        loExpr->Name = loName;

        // We expect '..' here; if it's not here emit error.
        expect(TokenKind::DotDot); // will emit error if not present
        auto node    = std::make_unique<ArrayTypeNode>();
        node->Loc    = Loc;
        node->Packed = Packed;
        node->Low    = std::move(loExpr);
        node->High   = parseExpression();
        expect(TokenKind::RightBracket);
        expect(TokenKind::Of);
        node->Element = parseTypeExpr();
        return node;
    }

    // lo is not an identifier — parse as regular array.
    auto node    = std::make_unique<ArrayTypeNode>();
    node->Loc    = Loc;
    node->Packed = Packed;
    node->Low    = parseExpression();
    expect(TokenKind::DotDot);
    node->High   = parseExpression();
    expect(TokenKind::RightBracket);
    expect(TokenKind::Of);
    node->Element = parseTypeExpr();
    return node;
}

// record-type → 'record' field-section* variant-part? ';'? 'end'
std::unique_ptr<RecordTypeNode> Parser::parseRecordType(bool Packed) {
    auto Node    = std::make_unique<RecordTypeNode>();
    Node->Loc    = Current;
    Node->Packed = Packed;
    expect(TokenKind::Record);

    while (!check(TokenKind::End) && !check(TokenKind::Case) && !check(TokenKind::Eof)) {
        FieldDecl Fd;
        Fd.Names.push_back(expect(TokenKind::Identifier).Lexeme);
        while (match(TokenKind::Comma)) {
            Fd.Names.push_back(expect(TokenKind::Identifier).Lexeme);
        }
        expect(TokenKind::Colon);
        Fd.Type = parseTypeExpr();
        // EP §6.6: a record-section's denoter may say what its fields start
        // as, which is how a record gets an initial state field by field.
        if (Fd.Type) parseInitialState(*Fd.Type);
        Node->Fields.push_back(std::move(Fd));

        if (!match(TokenKind::Semicolon)) break;
        // Allow trailing semicolons before 'end' or 'case'.
    }

    if (check(TokenKind::Case)) {
        Node->Variant = parseVariantPart();
        match(TokenKind::Semicolon); // optional trailing semicolon after last variant
    }

    expect(TokenKind::End);
    return Node;
}

// variant-part → 'case' [identifier ':'] type-expr 'of'
//                variant-case (';' variant-case)* ';'?
// variant-case → case-label-list ':' '(' field-list ')' | ε
std::unique_ptr<VariantPart> Parser::parseVariantPart() {
    auto Part = std::make_unique<VariantPart>();
    expect(TokenKind::Case);

    // Disambiguate: 'identifier ':'  type-expr' vs just 'type-expr'
    if (check(TokenKind::Identifier)) {
        Token Tok = Current;
        advance();
        if (match(TokenKind::Colon)) {
            // tag field name found
            Part->TagField = Tok.Lexeme;
            Part->TagType  = parseTypeExpr();
        } else if (check(TokenKind::DotDot)) {
            // identifier is subrange lower bound
            advance();
            auto Low  = std::make_unique<IdentExpr>();
            Low->Loc  = Tok; Low->Name = Tok.Lexeme;
            auto Sub  = std::make_unique<SubrangeTypeNode>();
            Sub->Loc  = Tok; Sub->Low = std::move(Low); Sub->High = parseFactor();
            Part->TagType = std::move(Sub);
        } else {
            // just a named type
            auto Nt  = std::make_unique<NamedTypeNode>();
            Nt->Loc  = Tok; Nt->Name = Tok.Lexeme;
            Part->TagType = std::move(Nt);
        }
    } else {
        Part->TagType = parseTypeExpr();
    }
    expect(TokenKind::Of);

    // Parse variant cases until 'end' (which belongs to the enclosing record).
    while (!check(TokenKind::End) && !check(TokenKind::Eof)) {
        VariantCase Vc;
        // Case label list
        Vc.Labels.push_back(parseFactor());
        while (match(TokenKind::Comma)) {
            Vc.Labels.push_back(parseFactor());
        }
        expect(TokenKind::Colon);
        expect(TokenKind::LeftParen);
        // Fields within this variant (may be empty, or may contain a nested variant part).
        while (!check(TokenKind::RightParen) && !check(TokenKind::Eof)) {
            // Nested variant case (ISO §6.4.3.3)
            if (check(TokenKind::Case)) {
                Vc.NestedVariant = parseVariantPart();
                break;
            }
            FieldDecl Fd;
            Fd.Names.push_back(expect(TokenKind::Identifier).Lexeme);
            while (match(TokenKind::Comma)) {
                Fd.Names.push_back(expect(TokenKind::Identifier).Lexeme);
            }
            expect(TokenKind::Colon);
            Fd.Type = parseTypeExpr();
            Vc.Fields.push_back(std::move(Fd));
            if (!match(TokenKind::Semicolon)) break;
        }
        expect(TokenKind::RightParen);
        Part->Cases.push_back(std::move(Vc));

        if (!match(TokenKind::Semicolon)) break;
        // Stop if next is 'end' (trailing semicolon before 'end' is allowed).
        if (check(TokenKind::End)) break;
    }

    return Part;
}
