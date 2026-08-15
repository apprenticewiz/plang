//===- ParseDecl.cpp - Parsing of programs, blocks, and declaration parts (§6.2, §6.6). ===//

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
// Program and block
// ---------------------------------------------------------------------------

// program → 'program' identifier ( '(' identifier-list ')' )? ';'
//            ['import' clauses]  block '.'
std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto Node  = std::make_unique<ProgramNode>();
    Node->Loc  = Current;
    expect(TokenKind::Program);
    Node->Name = expect(TokenKind::Identifier).Lexeme;
    // Optional file-parameter list: program graph1(input, output);
    if (match(TokenKind::LeftParen)) {
        Node->FileParams.push_back(expect(TokenKind::Identifier).Lexeme);
        while (match(TokenKind::Comma)) {
            Node->FileParams.push_back(expect(TokenKind::Identifier).Lexeme);
        }
        expect(TokenKind::RightParen);
    }
    expect(TokenKind::Semicolon);
    // EP §6.11.3: optional import clauses before the block.
    if (Opts.extendedPascal() && check(TokenKind::Import)) {
        Node->Imports = parseImportClauses();
    }
    Node->Block = parseBlock();
    expect(TokenKind::Dot);
    expect(TokenKind::Eof);
    return Node;
}

// block → label-section* const-section* type-section* var-section*
//         proc-section* compound-stmt
std::unique_ptr<BlockNode> Parser::parseBlock() {
    auto Node = std::make_unique<BlockNode>();
    Node->Loc  = Current;

    if (Opts.has(LangOptions::Feature::FreeDeclarationOrder)) {
        // EP §6.2.1: declaration sections may appear in any order and be repeated.
        bool More = true;
        while (More) {
            if      (check(TokenKind::Label))     { parseLabelSection(*Node); }
            else if (check(TokenKind::Const))     { parseConstSection(*Node); }
            else if (check(TokenKind::Type))      { parseTypeSection(*Node);  }
            else if (check(TokenKind::Var))       { parseVarSection(*Node);   }
            else if (check(TokenKind::Procedure) || check(TokenKind::Function))
                Node->Procs.push_back(parseProcDecl(check(TokenKind::Function)));
            else More = false;
        }
    } else {
        // ISO 7185: strict ordering — label, const, type, var, procedures.
        while (check(TokenKind::Label))     parseLabelSection(*Node);
        while (check(TokenKind::Const))     parseConstSection(*Node);
        while (check(TokenKind::Type))      parseTypeSection(*Node);
        while (check(TokenKind::Var))       parseVarSection(*Node);
        while (check(TokenKind::Procedure) || check(TokenKind::Function)) {
            bool IsFunction = check(TokenKind::Function);
            Node->Procs.push_back(parseProcDecl(IsFunction));
        }
    }

    Node->Body = parseCompoundStmt();
    return Node;
}

// label-section → 'label' label-id (',' label-id)* ';'
void Parser::parseLabelSection(BlockNode& Block) {
    expect(TokenKind::Label);
    // Labels can be unsigned integers or identifiers.
    if (check(TokenKind::IntLit) || check(TokenKind::Identifier)) {
        Block.Labels.push_back(canonicalLabel(Current.Lexeme));
        advance();
    } else {
        emitError(Current.toLoc(), diag::err_expected_label, {describe(Current.Kind)});
        advance();
    }
    while (match(TokenKind::Comma)) {
        if (check(TokenKind::IntLit) || check(TokenKind::Identifier)) {
            Block.Labels.push_back(canonicalLabel(Current.Lexeme));
            advance();
        } else {
            emitError(Current.toLoc(), diag::err_expected_label, {describe(Current.Kind)});
            advance();
        }
    }
    expect(TokenKind::Semicolon);
}

// ---------------------------------------------------------------------------
// Constant definitions
// ---------------------------------------------------------------------------

// const-section → 'const' const-def+
void Parser::parseConstSection(BlockNode& Block) {
    expect(TokenKind::Const);
    do {
        Block.Consts.push_back(parseConstDef());
    } while (check(TokenKind::Identifier));
}

// const-def → identifier '=' expr ';'
// EP §6.8.2: any nonvarying expression is permitted as a constant value.
ConstDef Parser::parseConstDef() {
    ConstDef Def;
    Def.Name  = expect(TokenKind::Identifier).Lexeme;
    expect(TokenKind::Equal);
    Def.Value = Opts.has(LangOptions::Feature::ConstantExpressions) ? parseExpression()
                                                          : parseSimpleExpr();
    expect(TokenKind::Semicolon);
    return Def;
}

// ---------------------------------------------------------------------------
// Type definitions
// ---------------------------------------------------------------------------

// type-section → 'type' type-def+
void Parser::parseTypeSection(BlockNode& Block) {
    expect(TokenKind::Type);
    do {
        Block.Types.push_back(parseTypeDef());
    } while (check(TokenKind::Identifier));
}

// type-def → identifier [ '(' schema-params ')' ] '=' [ 'bindable' ] type-expr ';'
// EP §6.4.7: schema definition when param list follows the name.
TypeDef Parser::parseTypeDef() {
    TypeDef Def;
    Def.Name = expect(TokenKind::Identifier).Lexeme;
    TypeNames_.insert(toLower(Def.Name));

    // EP §6.4.7: optional schema discriminant list — SchemaName(n: integer; m: integer)
    if (Opts.extendedPascal() && match(TokenKind::LeftParen)) {
        // Helper: consume the current token as a discriminant type name.
        // Accepts both user-defined identifiers and built-in type keywords
        // (integer, real, boolean, char) since all are valid ordinal type names.
        auto consumeTypeName = [&]() -> std::string {
            std::string Name = Current.Lexeme;
            advance();
            return Name;
        };
        do {
            SchemaParamSpec Spec;
            Spec.Names.push_back(expect(TokenKind::Identifier).Lexeme);
            while (match(TokenKind::Comma))
                Spec.Names.push_back(expect(TokenKind::Identifier).Lexeme);
            expect(TokenKind::Colon);
            // Accept identifier or built-in type keyword as type name.
            Spec.TypeName = consumeTypeName();
            Def.SchemaParams.push_back(std::move(Spec));
        } while (match(TokenKind::Semicolon));
        expect(TokenKind::RightParen);
    }

    expect(TokenKind::Equal);

    // EP §6.4.1: optional 'bindable' prefix on the type expression.
    if (Opts.extendedPascal() && check(TokenKind::Bindable)) {
        advance();
        Def.IsBindable = true;
    }

    Def.Type = parseTypeExpr();
    if (Def.IsBindable && Def.Type) Def.Type->Bindable = true;
    if (Def.Type) parseInitialState(*Def.Type);
    expect(TokenKind::Semicolon);
    return Def;
}

// ---------------------------------------------------------------------------
// Variable declarations
// ---------------------------------------------------------------------------

// var-section → 'var' var-group+
void Parser::parseVarSection(BlockNode& Block) {
    expect(TokenKind::Var);
    do {
        Block.Vars.push_back(parseVarGroup());
    } while (check(TokenKind::Identifier));
}

// var-group → identifier-list ':' type-expr [ 'value' expression ] ';'
// EP §6.4.1: 'value' introduces an initial-state specifier.
VarGroup Parser::parseVarGroup() {
    VarGroup G;
    G.Names.push_back(expect(TokenKind::Identifier).Lexeme);
    while (match(TokenKind::Comma)) {
        G.Names.push_back(expect(TokenKind::Identifier).Lexeme);
    }
    for (const auto& N : G.Names) VarNames_.insert(toLower(N));
    expect(TokenKind::Colon);
    G.Type = parseTypeExpr();
    // EP §6.6: the initial-state-specifier of the variable's own denoter.  It
    // is kept on the declaration rather than on the type node because it is
    // this declaration's variables it initialises and no others.
    if (Opts.extendedPascal() && match(TokenKind::Value)) {
        G.InitExpr = parseComponentValue();
    }
    expect(TokenKind::Semicolon);
    return G;
}

// ---------------------------------------------------------------------------
// Procedure and function declarations
// ---------------------------------------------------------------------------

// proc-decl → 'procedure' identifier param-list ';' (block ';' | 'forward' ';')
// func-decl → 'function'  identifier param-list ':' type-expr ';' (block ';' | 'forward' ';')
std::unique_ptr<ProcDecl> Parser::parseProcDecl(bool IsFunction,
                                                bool HeadingOnly) {
    auto Node        = std::make_unique<ProcDecl>();
    Node->Loc        = Current;
    Node->IsFunction = IsFunction;
    Node->IsForward  = false;

    if (IsFunction) {
        expect(TokenKind::Function);
    } else {
        expect(TokenKind::Procedure);
    }

    Node->Name   = expect(TokenKind::Identifier).Lexeme;
    Node->Params = parseParamList();

    if (IsFunction) {
        // EP §6.7.2: optional result-variable-specification: '=' identifier
        if (match(TokenKind::Equal)) {
            Node->ResultName = expect(TokenKind::Identifier).Lexeme;
        }
        // ISO §6.6.1: the body of a function declared 'forward' is introduced
        // by the name alone, the result type having been given already.  Only
        // Sema can tell that from a function heading that has simply lost its
        // result type, so an absent ':' is carried through rather than
        // reported here.
        if (match(TokenKind::Colon))
            Node->ReturnType = parseTypeExpr();
    }

    expect(TokenKind::Semicolon);

    // A 'forward' directive means this is only a declaration; the body follows later.
    if (match(TokenKind::Forward)) {
        Node->IsForward = true;
        expect(TokenKind::Semicolon);
        return Node;
    }

    // EP §6.11.2: in an interface the heading is the whole declaration.  It is
    // a forward declaration in every respect that matters here — a signature
    // with the body given elsewhere — so it is recorded as one.
    if (HeadingOnly) {
        Node->IsForward = true;
        return Node;
    }

    Node->Body = parseBlock();
    expect(TokenKind::Semicolon);
    return Node;
}

// param-list → ( '(' param-group (';' param-group)* ')' )?
std::vector<ParamGroup> Parser::parseParamList() {
    if (!match(TokenKind::LeftParen)) return {};
    std::vector<ParamGroup> Params;
    if (!check(TokenKind::RightParen)) {
        Params.push_back(parseParamGroup());
        while (match(TokenKind::Semicolon)) {
            Params.push_back(parseParamGroup());
        }
    }
    expect(TokenKind::RightParen);
    return Params;
}

// param-group → ['protected'] ['var'] identifier-list ':' type-expr
//             | 'var' identifier-list ':' type-expr
// EP §6.7.3.1: 'protected' marks value parameters as non-assignable.
// (protected var is also accepted though unusual; protected applies to the value copy)
ParamGroup Parser::parseParamGroup() {
    ParamGroup G;
    // ISO §6.6.3.1: a section that starts with 'procedure' or 'function' is a
    // procedural or functional parameter, written as the whole heading of what
    // it will receive rather than as 'name : type'.
    if (check(TokenKind::Procedure) || check(TokenKind::Function))
        return parseProcedureParamGroup();
    // EP §6.7.3.1: optional 'protected' prefix (EP mode only; in ISO 7185
    // mode 'protected' is scanned as Identifier, so this check is safe).
    if (Opts.extendedPascal() && check(TokenKind::Protected)) {
        G.IsProtected = true; advance();
    }
    G.IsVar = match(TokenKind::Var);
    G.Names.push_back(expect(TokenKind::Identifier).Lexeme);
    while (match(TokenKind::Comma)) {
        G.Names.push_back(expect(TokenKind::Identifier).Lexeme);
    }
    for (const auto& N : G.Names) VarNames_.insert(toLower(N));
    expect(TokenKind::Colon);
    // ISO §6.6.3.7: a conformant array schema is a parameter type and nothing
    // else — never a type definition, never a variable.  Both forms of it
    // begin like an ordinary array, so both are read here.
    if (check(TokenKind::Array)) {
        G.Type = parseConformantOrRegular(/*Packed=*/false);
    } else if (check(TokenKind::Packed)) {
        Token PLoc = Current;
        advance();
        G.Type = parsePackedTypeTail(PLoc, /*ConformantAllowed=*/true);
    } else {
        G.Type = parseTypeExpr();
    }
    return G;
}

// procedural-parameter  → 'procedure' identifier param-list
// functional-parameter  → 'function'  identifier param-list ':' type-identifier
//
// ISO §6.6.3.1.  Unlike every other section this one names exactly one
// parameter, because the heading it is written as carries the name.
ParamGroup Parser::parseProcedureParamGroup() {
    const bool IsFunction = check(TokenKind::Function);
    const Token Kw = Current;
    advance();

    auto PT        = std::make_unique<ProcedureTypeNode>();
    PT->Loc        = Kw;
    PT->IsFunction = IsFunction;

    ParamGroup G;
    G.Names.push_back(expect(TokenKind::Identifier).Lexeme);
    PT->Params = parseParamList();

    if (IsFunction) {
        expect(TokenKind::Colon);
        PT->ReturnType = parseTypeExpr();
    }

    G.Type = std::move(PT);
    return G;
}
