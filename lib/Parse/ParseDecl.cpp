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

// Ceiling on live parseBlock activations (Parser::BlockDepth).  Mirrors
// MaxExprDepth in ParseExpr.cpp: 500 levels of nesting is nowhere near
// exhausting an 8MB default stack -- a procedure declared inside a procedure
// declared inside another, arbitrarily deep, crashes only tens of thousands
// of levels deeper than this on this machine -- while no legitimate Extended
// Pascal program nests procedure or function declarations anywhere close to
// this deep.  Without a ceiling here, a source file built specifically to
// nest deeply (or a generated/fuzzed one) drives the real call stack instead
// of a diagnostic.
static constexpr unsigned MaxBlockDepth = 500;

// ---------------------------------------------------------------------------
// Program and block
// ---------------------------------------------------------------------------

// program → 'program' identifier ( '(' identifier-list ')' )? ';'
//            ['import' clauses]  block '.'
//
// Turbo drops the heading line entirely: a bare 'begin ... end.' is a
// complete program.  (ISO 7185 already makes the file-parameter LIST
// optional -- 'program name;' with no '(...)' -- the loop below; Turbo goes
// one step further and makes the whole 'program name;' line optional too.)
// Gated on Opts.turbo() directly rather than a LangFeatures.def entry: this
// is Turbo's alone, not a capability ISO 10206 shares (EP's own §6.10.1
// still requires the heading), so it asks the "which dialect" question
// (LangOptions.h's own comment on extendedPascal()/turbo() explains the
// distinction) instead of adding a one-dialect FEATURE.
std::unique_ptr<ProgramNode> Parser::parseProgram() {
    auto Node  = std::make_unique<ProgramNode>();
    Node->Loc  = Current;
    if (Opts.turbo() && !check(TokenKind::Program)) {
        // No heading at all: codegen and diagnostics still want SOME name
        // (it becomes the LLVM module's identifier -- Codegen::Impl::init),
        // and this project's own convention for a name with nothing real to
        // give -- Type::makeError()'s "<error>", Scanner's "<pmi>" for its
        // own not-a-real-file source name -- is a bracketed placeholder
        // rather than an empty string.
        Node->Name = "<program>";
    } else {
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
    }
    // EP §6.11.3: optional import clauses before the block.
    if (Opts.extendedPascal() && check(TokenKind::Import)) {
        Node->Imports = parseImportClauses();
    }
    // Turbo Tier 4, Cluster A item 1: a program's own top-level 'uses'
    // clause, the same grammar production as a unit's own (parseUsesClause,
    // ParseUnit.cpp) -- 'uses' identifier (',' identifier)* ';', with no
    // 'qualified'/selective/renaming syntax of EP's ImportClause at all.
    // Turbo-only: gated on Opts.turbo() the same way the no-heading form
    // above is, since ISO 7185/Extended Pascal programs use Imports instead.
    if (Opts.turbo() && check(TokenKind::Uses)) {
        Node->Uses = parseUsesClause();
    }
    Node->Block = parseBlock();
    expect(TokenKind::Dot);
    expect(TokenKind::Eof);
    return Node;
}

// block → label-section* const-section* type-section* var-section*
//         proc-section* compound-stmt
std::unique_ptr<BlockNode> Parser::parseBlock() {
    // A block recurses into another block only through a nested procedure or
    // function's own body (parseProcDecl below), so this is the one place a
    // ceiling bounds that whole mutually-recursive cycle.  Checked before the
    // RAII bump a few lines down: a caller already sitting at the ceiling
    // must return without recursing again, not recurse once more and only
    // then stop.
    if (BlockDepth >= MaxBlockDepth) {
        if (!BlockDepthLimitHit) {
            BlockDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_proc_too_deeply_nested);
        }
        // Deliberately does not consume Current, and returns a block with no
        // declarations and no body -- every caller up the stack is a
        // parseProcDecl still waiting on its own trailing ';' and unwinds on
        // its own once it sees the same token still there (suppressed below).
        auto Node = std::make_unique<BlockNode>();
        Node->Loc = Current;
        return Node;
    }
    BlockDepthScope DepthGuard(BlockDepth, BlockDepthLimitHit);

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
            else if (check(TokenKind::Procedure) || check(TokenKind::Function) ||
                     // Turbo Tier 5: an object-type method's out-of-line
                     // BODY starts with 'constructor'/'destructor' rather
                     // than 'procedure'/'function' -- parseProcDecl itself
                     // tells the two apart (and reads the dotted
                     // 'TypeName.MethodName' qualifier that makes this a
                     // method body and not an ordinary top-level routine).
                     (Opts.turbo() && (check(TokenKind::Constructor) ||
                                       check(TokenKind::Destructor))))
                Node->Procs.push_back(parseProcDecl(check(TokenKind::Function)));
            else More = false;
        }
    } else {
        // ISO 7185: strict ordering — label, const, type, var, procedures.
        while (check(TokenKind::Label))     parseLabelSection(*Node);
        while (check(TokenKind::Const))     parseConstSection(*Node);
        while (check(TokenKind::Type))      parseTypeSection(*Node);
        while (check(TokenKind::Var))       parseVarSection(*Node);
        while (check(TokenKind::Procedure) || check(TokenKind::Function) ||
               (Opts.turbo() && (check(TokenKind::Constructor) ||
                                 check(TokenKind::Destructor)))) {
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
//           | identifier ':' type-expr '=' turbo-const-value ';'   (-std=turbo)
// EP §6.8.2: any nonvarying expression is permitted as a constant value.
//
// Turbo's typed-constant form is checked for first (only under -std=turbo,
// and only when a ':' actually follows the name): every other dialect falls
// straight through to the classic form below, and a ':' where '=' was wanted
// becomes the same "expected '='" syntax error it always was, which is
// exactly the rejection -std=iso7185/-std=iso10206 want for this Turbo-only
// syntax -- no separate dialect diagnostic is needed for it.
ConstDef Parser::parseConstDef() {
    ConstDef Def;
    Def.Name  = expect(TokenKind::Identifier).Lexeme;
    if (Opts.turbo() && check(TokenKind::Colon)) {
        advance(); // ':'
        Def.Type = parseTypeExpr();
        expect(TokenKind::Equal);
        Def.Value = parseTurboConstValue();
        expect(TokenKind::Semicolon);
        return Def;
    }
    expect(TokenKind::Equal);
    Def.Value = Opts.has(LangOptions::Feature::ConstantExpressions) ? parseExpression()
                                                          : parseSimpleExpr();
    expect(TokenKind::Semicolon);
    return Def;
}

// See this method's own declaration (Parser.h) for the overall design.  Not
// bounded by MaxValueDepth/ValueDepthScope the way parseComponentValue is:
// that guard exists for EP's structured-value-constructor grammar, which
// this is a sibling of but a separate recursion through (see
// TurboConstValueDepth's own comment, Parser.h).  A typed constant nested
// arbitrarily deep ('(((...)))' by hand or by a fuzzer) would otherwise
// drive the real call stack instead of a diagnostic the same way an
// unbounded parseComponentValue used to.
static constexpr unsigned MaxTurboConstValueDepth = 500;

std::unique_ptr<ExprNode> Parser::parseTurboConstValue() {
    if (!check(TokenKind::LeftParen)) return parseExpression();

    // Checked before the RAII bump just below: a caller already sitting at
    // the ceiling must return without recursing again, not recurse once more
    // and only then stop.  Mirrors parseComponentValue's own guard
    // (ParseInit.cpp).
    if (TurboConstValueDepth >= MaxTurboConstValueDepth) {
        if (!TurboConstValueDepthLimitHit) {
            TurboConstValueDepthLimitHit = true;
            emitError(Current.toLoc(), diag::err_value_too_deeply_nested);
        }
        auto Node   = std::make_unique<IntLitExpr>();
        Node->Loc   = Current;
        Node->Value = 0;
        return Node;
    }
    TurboConstValueDepthScope DepthGuard(TurboConstValueDepth, TurboConstValueDepthLimitHit);

    Token Loc = Current;
    advance(); // '('
    auto Node = std::make_unique<StructuredValueExpr>();
    Node->Loc = Loc;

    if (match(TokenKind::RightParen)) return Node; // '()' -- Sema diagnoses.

    // Parses one arm.  Sets IsField to say whether it turned out to be a
    // 'name : value' record arm (decided per-arm from a single token of
    // lookahead: an identifier that parseExpression stops at cleanly right
    // before a ':', since ':' never continues an expression) or a bare
    // positional array element.
    auto parseArm = [&](bool& IsField) {
        StructuredValueArm Arm;
        if (check(TokenKind::Identifier)) {
            Token IdTok = Current;
            auto E = parseExpression();
            if (check(TokenKind::Colon) && llvm::isa<IdentExpr>(E.get())) {
                advance(); // ':'
                auto Id  = std::make_unique<IdentExpr>();
                Id->Loc  = IdTok.Loc;
                Id->Name = IdTok.Lexeme;
                Arm.Labels.push_back(std::move(Id));
                Arm.Value = parseTurboConstValue();
                IsField = true;
            } else {
                Arm.Value = std::move(E);
                IsField = false;
            }
        } else {
            Arm.Value = parseTurboConstValue();
            IsField = false;
        }
        return Arm;
    };

    bool IsField = false;
    Node->Arms.push_back(parseArm(IsField));
    const TokenKind Sep = IsField ? TokenKind::Semicolon : TokenKind::Comma;
    while (match(Sep)) {
        bool ArmIsField = IsField;
        Node->Arms.push_back(parseArm(ArmIsField));
    }
    expect(TokenKind::RightParen);
    return Node;
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
    {
        Token T = expect(TokenKind::Identifier);
        G.Names.push_back(T.Lexeme);
        G.NameLocs.push_back(T.Loc);
    }
    while (match(TokenKind::Comma)) {
        Token T = expect(TokenKind::Identifier);
        G.Names.push_back(T.Lexeme);
        G.NameLocs.push_back(T.Loc);
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
    // Turbo's 'absolute' directive: 'var W: Word absolute B;'.  Deliberately
    // NOT a reserved word (TokenKinds.def has no token for it) -- it is
    // recognized only by its spelling, only in this one position, right
    // after a var-declaration's type -- so a program that declares its own
    // identifier called 'absolute' anywhere else is completely unaffected.
    // Same idiom ParseModule.cpp uses for EP's contextual 'implementation'.
    if (Opts.turbo() && check(TokenKind::Identifier)
            && toLower(Current.Lexeme) == "absolute") {
        advance(); // 'absolute'
        Token TargetTok = expect(TokenKind::Identifier);
        auto Ident  = std::make_unique<IdentExpr>();
        Ident->Loc  = TargetTok.Loc;
        Ident->Name = TargetTok.Lexeme;
        // The overlay target may itself be a component -- 'absolute B[0]',
        // 'absolute R.Field' -- so it is parsed as a full postfix designator,
        // the same as an assignment statement's left-hand side is.
        G.AbsoluteExpr = parsePostfix(std::move(Ident));
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
    } else if (Opts.turbo() && check(TokenKind::Constructor)) {
        // Turbo Tier 5: 'constructor Init(...)' -- the out-of-line BODY of an
        // object-type constructor.  Handled here rather than a separate
        // entry point so this single function stays the one place a
        // top-level/block-level procedure-shaped declaration is parsed --
        // see the dotted-heading handling below for the other half of what
        // makes this a Turbo object method rather than an ordinary
        // procedure.
        advance();
        Node->IsConstructor = true;
    } else if (Opts.turbo() && check(TokenKind::Destructor)) {
        advance();
        Node->IsDestructor = true;
    } else {
        expect(TokenKind::Procedure);
    }

    Node->Name = expect(TokenKind::Identifier).Lexeme;
    // Turbo Tier 5: an object-type method's out-of-line BODY repeats the
    // heading qualified by its owning type -- 'procedure TAnimal.Speak;
    // begin ... end;' -- confirmed against a local fpc -Mtp build (the
    // dotted qualifier is a plain type name, exactly like the constructs
    // ImportClause::ModuleName/ProcDecl::OwnerType already model as an
    // unresolved cross-reference string).  Gated on Opts.turbo(): the '.'
    // cannot follow a bare procedure/function name in any other dialect, so
    // this can never misparse an EP/ISO 7185 program.
    if (Opts.turbo() && check(TokenKind::Dot)) {
        advance();
        Node->OwnerType = Node->Name;
        Node->Name      = expect(TokenKind::Identifier).Lexeme;
    }
    Node->Params = parseParamList();

    if (IsFunction) {
        // EP §6.6: optional result-variable-specification: '=' identifier.
        // Recognized (and still parsed, for clean recovery) under every
        // dialect -- '=' cannot start anything else here, ISO 7185's grammar
        // going straight from the parameter list to ':'/';' -- but only kept
        // as EP's own extension; issue found during a review of Turbo's
        // {$X} declaration-parsing area: this had no dialect gate at all, so
        // `-std=iso7185` silently accepted it.
        if (match(TokenKind::Equal)) {
            if (!Opts.extendedPascal())
                emitError(Current.toLoc(), diag::err_ep_named_result);
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
    // Suppressed while unwinding from the depth ceiling above: every
    // enclosing procedure or function between here and the ceiling is
    // missing its trailing ';' for the same reason, and expect()'s
    // diagnostic once per level would bury the one diagnostic that actually
    // explains the failure.
    if (BlockDepthLimitHit)
        match(TokenKind::Semicolon);
    else
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
//             | 'const' identifier-list ':' type-expr            (turbo)
//             | 'var' identifier-list                            (turbo, untyped)
// EP §6.7.3.1: 'protected' marks value parameters as non-assignable.
// (protected var is also accepted though unusual; protected applies to the value copy)
// Turbo: 'const' marks a parameter read-only, passed by reference for a
// structured type (CodeGenProcs.cpp); a 'var' parameter with no ': type' at
// all is Turbo's UNTYPED parameter (ParamGroup::Type stays null) -- checked
// against a local fpc -Mtp build that only this VAR form is legal (a bare
// name with no 'var' and no type is rejected outright).
ParamGroup Parser::parseParamGroup() {
    ParamGroup G;
    G.Loc = Current.Loc;
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
    // Turbo's 'const' parameter prefix -- mutually exclusive with 'var' in
    // real Turbo Pascal, so this is simply an alternative to the 'var' match
    // just below rather than combined with it.
    if (Opts.turbo() && check(TokenKind::Const)) {
        G.IsConst = true; advance();
    }
    G.IsVar = match(TokenKind::Var);
    {
        Token T = expect(TokenKind::Identifier);
        G.Names.push_back(T.Lexeme);
        G.NameLocs.push_back(T.Loc);
    }
    while (match(TokenKind::Comma)) {
        Token T = expect(TokenKind::Identifier);
        G.Names.push_back(T.Lexeme);
        G.NameLocs.push_back(T.Loc);
    }
    for (const auto& N : G.Names) VarNames_.insert(toLower(N));
    // Turbo's untyped parameter: 'var x' (or 'var x, y') with no ': type' to
    // follow at all.  G.Type stays null -- see its own comment (AstType.h)
    // for the whole design and the audit that keeps every dereference of it
    // elsewhere in the compiler null-safe.
    if (Opts.turbo() && G.IsVar && !check(TokenKind::Colon)) {
        return G;
    }
    expect(TokenKind::Colon);
    // ISO §6.6.3.7 / Turbo: an array-shaped parameter form.  EP/ISO 7185
    // Level 1's conformant-array schema (array [lo..hi: T] of E) and
    // Turbo's own open-array form (array of T) are syntactically
    // distinguishable -- Turbo's never has a bracket at all -- so this is a
    // clean per-dialect branch rather than an ambiguous grammar; see
    // parseTurboOpenArrayParamType's own comment for the Turbo form and
    // parseConformantOrRegular's for the EP one.  Neither form is legal
    // outside its own dialect: EP's falls through to parseTypeExpr under
    // Turbo, which parseTurboOpenArrayParamType itself diagnoses
    // (err_turbo_conformant_array) rather than silently accepting; Turbo's
    // falls through to parseConformantOrRegular under EP/ISO 7185, which
    // fails its own expect(LeftBracket) the ordinary way.
    if (check(TokenKind::Array)) {
        G.Type = Opts.turbo() ? parseTurboOpenArrayParamType()
                              : parseConformantOrRegular(/*Packed=*/false);
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
    {
        Token T = expect(TokenKind::Identifier);
        G.Names.push_back(T.Lexeme);
        G.NameLocs.push_back(T.Loc);
    }
    PT->Params = parseParamList();

    if (IsFunction) {
        expect(TokenKind::Colon);
        PT->ReturnType = parseTypeExpr();
    }

    G.Type = std::move(PT);
    return G;
}
