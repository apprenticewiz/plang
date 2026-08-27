#pragma once

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Lex/Scanner.h"
#include "plang/Basic/Token.h"

#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Recursive descent parser for the ISO Pascal subset supported by plang.
///
/// The parser owns a Scanner and buffers exactly one token at a time (Current),
/// which always holds the next unconsumed token. Calling advance() fetches the
/// next token from the scanner into Current.
///
/// All syntax errors are appended to the shared diagnostics vector; the parser
/// never throws.  On a missing token, expect() uses insert-mode recovery
/// (returns a synthetic token without consuming the current one) so that outer
/// parse methods can usually continue and collect multiple errors per file.
/// On an unexpected token in expression position the bad token is consumed to
/// prevent infinite loops.
class Parser {
public:
    /// Constructs a parser that will read from the given scanner.
    /// Primes Current by calling advance() once before returning.
    explicit Parser(Scanner Sc, DiagnosticsEngine& Diags,
                    LangOptions Opts = {});

    /// Parses a complete Pascal program and returns the root AST node, or
    /// nullptr if any parse errors were encountered.  In the nullptr case the
    /// errors are already recorded in the shared diagnostics vector.
    [[nodiscard]] std::unique_ptr<ProgramNode> parse();

private:
    LangOptions              Opts;         // dialect and warning options (owned copy)
    Scanner                  Lex;          // token source; owned by the parser
    Token                    Current;      // the next unconsumed token
    DiagnosticsEngine&       Diags;       // shared diagnostic sink
    int                      ErrorCount{}; // errors emitted by this parse pass

    // Live activations of parseFactor.  Every recursive re-entry into
    // expression parsing -- '(' via parseExpression, 'not' directly -- funnels
    // through parseFactor, so bounding activations there (see ExprDepthScope
    // and MaxExprDepth in ParseExpr.cpp) bounds the whole mutually-recursive
    // parseExpression/parseSimpleExpr/parseTerm/parsePower/parseFactor cycle
    // against adversarial input like x := ((((...(1)...)))), which used to
    // exhaust the real C++ stack instead of failing with a diagnostic.
    unsigned                 ExprDepth{};
    // Set once the "too deeply nested" diagnostic has fired for the chain of
    // parseFactor activations currently unwinding, so that the burst of
    // "expected )" diagnostics each stacked '(' would otherwise report on the
    // way out collapses to the single diagnostic that actually explains the
    // failure.  Cleared by ExprDepthScope when ExprDepth returns to 0.
    bool                     ExprDepthLimitHit{};

    // RAII bump/unbump of ExprDepth across one parseFactor activation.
    struct ExprDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit ExprDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~ExprDepthScope() { if (--N == 0) LimitHit = false; }
    };

    // Live activations of parseTypeExpr.  Every recursive re-entry into type
    // parsing -- an array's element type, a pointer's base type, a record
    // field's type, a set-of/file-of base type -- funnels through
    // parseTypeExpr, so bounding activations there (see TypeDepthScope and
    // MaxTypeDepth in ParseType.cpp) bounds all of it against adversarial
    // input like 'array of array of array of ...', '^^^^...integer', or
    // 'record f: record f: record f: ... end end end', which used to exhaust
    // the real C++ stack instead of failing with a diagnostic.  Same shape as
    // ExprDepth immediately above; see its comment for the rationale.
    unsigned                 TypeDepth{};
    bool                     TypeDepthLimitHit{};
    struct TypeDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit TypeDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~TypeDepthScope() { if (--N == 0) LimitHit = false; }
    };

    // Live activations of parseStatement.  Every recursive re-entry into
    // statement parsing -- a compound statement's members, an if/while/for/
    // repeat/with statement's body, a case arm, a labeled statement's target
    // -- funnels through parseStatement, so bounding activations there (see
    // StmtDepthScope and MaxStmtDepth in ParseStmt.cpp) bounds all of it
    // against adversarial input like deeply nested 'begin ... end' or
    // 'if true then if true then ...', which used to exhaust the real C++
    // stack instead of failing with a diagnostic.  Same shape as ExprDepth
    // above; see its comment for the rationale.
    unsigned                 StmtDepth{};
    bool                     StmtDepthLimitHit{};
    struct StmtDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit StmtDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~StmtDepthScope() { if (--N == 0) LimitHit = false; }
    };

    // Live activations of parseBlock.  A block recurses into another block
    // only through a nested procedure or function's own body -- 'procedure p;
    // procedure q; ... begin end; begin end.' -- so bounding activations of
    // parseBlock itself (see BlockDepthScope and MaxBlockDepth in
    // ParseDecl.cpp) bounds the whole mutually-recursive parseBlock/
    // parseProcDecl cycle against a source file built to nest procedure or
    // function declarations arbitrarily deep.  Same shape as ExprDepth above;
    // see its comment for the rationale.
    unsigned                 BlockDepth{};
    bool                     BlockDepthLimitHit{};
    struct BlockDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit BlockDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~BlockDepthScope() { if (--N == 0) LimitHit = false; }
    };

    // Live activations of parseComponentValue and parseVariantPartValue
    // together.  EP §6.8.7's structured-value-constructor grammar is a
    // four-function cycle -- parseComponentValue <-> parseValueArms <->
    // parseVariantPartValue <-> parseFieldListValue -- but unlike the other
    // cycles above it has two funnel points rather than one: an arm's value
    // recurses straight back into parseComponentValue ('value [1:[1: ... :0]]'),
    // while a variant part's field-list recurses straight back into
    // parseVariantPartValue ('value [case 1 of [case 1 of [... ]]]') without
    // ever passing through parseComponentValue again.  parseValueArms and
    // parseFieldListValue are plain iterative dispatchers that only ever
    // reach deeper nesting by calling one of the two guarded functions, so
    // bounding activations of both of them (see ValueDepthScope and
    // MaxValueDepth in ParseInit.cpp) bounds the whole cycle against
    // adversarial input shaped like either example above, which used to
    // exhaust the real C++ stack instead of failing with a diagnostic.
    unsigned                 ValueDepth{};
    bool                     ValueDepthLimitHit{};
    struct ValueDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit ValueDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~ValueDepthScope() { if (--N == 0) LimitHit = false; }
    };

    // Appends an error diagnostic to the shared vector.
    void emitError(SourceLocation Loc, std::string Msg);
    void emitError(SourceLocation Loc, DiagID ID,
                   std::initializer_list<std::string_view> Args = {});

    // Fetches the next token from the scanner into Current.
    void advance();

    // Returns true if Current has the given kind, without consuming it.
    bool check(TokenKind Kind) const;

    // If Current has the given kind, consumes it and returns it.
    // Otherwise emits a diagnostic and returns a synthetic token of the
    // expected kind (insert-mode recovery — does NOT consume Current).
    Token expect(TokenKind Kind);

    // If Current has the given kind, consumes it and returns true.
    // Otherwise leaves Current unchanged and returns false.
    bool match(TokenKind Kind);

    // ---------------------------------------------------------------------------
    // Type expression parsing
    // ---------------------------------------------------------------------------

    // type-expr → named-type | array-type | record-type | pointer-type
    //           | enum-type | subrange-type | set-type | file-type | packed-type
    // named-type    → integer | real | boolean | string | char | identifier
    // array-type    → 'packed'? 'array' '[' index-type {',' index-type} ']' 'of' type-expr
    // index-type    → ordinal-type-name | enum-type | expr '..' expr
    // record-type   → 'packed'? 'record' field-section* variant-part? 'end'
    // pointer-type  → '^' type-expr
    // enum-type     → '(' identifier (',' identifier)* ')'
    // subrange-type → constant '..' constant   (integer, char, bool, or named constant)
    // set-type      → 'packed'? 'set' 'of' type-expr
    // file-type     → 'file' ('of' type-expr)?
    // packed-type   → 'packed' type-expr       (when used alone before non-array/record/set)
    std::unique_ptr<TypeNode>       parseTypeExpr();

    // array-type → 'array' '[' index-type {',' index-type} ']' 'of' type-expr
    // Called with Current pointing at 'array'; Packed is passed in from caller.
    std::unique_ptr<ArrayTypeNode>  parseArrayType(bool Packed);

    // One index of an array type.  A range comes back as a SubrangeTypeNode,
    // anything else as the ordinal type the index is named by.
    std::unique_ptr<TypeNode>       parseArrayIndexType();

    // One bound of a subrange type; accepts a sign (ISO §6.4.2.4).
    std::unique_ptr<ExprNode>       parseSubrangeBound();

    /// Modules imported with `qualified`, folded to lower case.  A reference to
    /// one of these followed by '.' is a qualified name (EP §6.11.2) rather
    /// than a field selection.
    std::set<std::string>           QualifiedModules_;

    /// Every type name defined so far, folded to lower case.  EP §6.8.7 spells
    /// a structured value TypeName[...], which reads exactly like indexing a
    /// variable, so the two can only be told apart by knowing which names are
    /// types.  A type must be defined before it is used, so a name that is not
    /// in here is not a type.
    std::set<std::string>           TypeNames_;
    /// Every name declared as a VARIABLE or a parameter, lowercased.
    ///
    /// `name[...]` is a typed set constructor when the name is a TYPE and an
    /// array subscript when it is a variable, and the parser has to choose
    /// before Sema has resolved anything.  It chose on TypeNames_ alone, so a
    /// variable shadowing a type name -- ordinary ISO 7185, no EP needed --
    /// had `g[i,j]` parsed as a set constructor and rejected.  Like TypeNames_
    /// this is flat and has no scope chain; it decides only which of the two
    /// SHAPES to build, and Sema resolves the name properly afterwards.
    std::set<std::string>           VarNames_;

    // record-type → 'record' field-section* variant-part? 'end'
    // Called with Current pointing at 'record'; Packed is passed in from caller.
    std::unique_ptr<RecordTypeNode> parseRecordType(bool Packed);

    // EP §6.7.3.7: parse a parameter-group type starting at 'array'.
    // If the '[' is followed by 'identifier .. identifier :' it is a conformant
    // array schema; otherwise it falls back to a regular array type.
    // Both branches consume 'array'; Packed is threaded through.
    std::unique_ptr<TypeNode> parseConformantOrRegular(bool Packed);
    /// The rest of a packed type, 'packed' having been consumed at \p Loc.
    /// ConformantAllowed is set in a formal parameter list, the one place ISO
    /// §6.6.3.7.1 admits a packed conformant array schema.
    std::unique_ptr<TypeNode> parsePackedTypeTail(Token Loc,
                                                  bool ConformantAllowed);

    // variant-part → 'case' [identifier ':'] type-expr 'of' variant-case*
    // Called with Current pointing at 'case'.
    std::unique_ptr<VariantPart>    parseVariantPart();

    // ---------------------------------------------------------------------------
    // Grammar rules — one method per non-terminal
    // ---------------------------------------------------------------------------

    // EP §6.11: file may begin with module definitions before the program.
    // Collects all module nodes then delegates to parseProgram().
    std::unique_ptr<ProgramNode>  parseMultiUnitFile();

    // EP §6.11: module-heading or module-body.
    // 'module' identifier [param-list] ['interface'] ';' ... 'end' '.'
    /// EP §6.11.1: reads one module-declaration, which is a heading, a block,
    /// or a heading and the block that follows it — one node for each.
    void                          parseModuleDeclaration(
                                      std::vector<std::unique_ptr<ModuleNode>>& Out);
    void                          parseModuleBlock(ModuleNode& Node);

    // The declaration sections of a module, in any order.  HeadingsOnly is set
    // for an interface, where a procedure or function is written as its
    // heading alone and the implementation module supplies the body.
    std::unique_ptr<BlockNode>    parseModuleDeclarations(bool HeadingsOnly);

    // EP §6.11.2: export specification after 'export' keyword.
    std::vector<ExportItem>       parseExportSection();

    // export-clause { ',' export-clause }, appended to Items.
    void                          parseExportList(std::vector<ExportItem>& Items);

    // '=>' identifier, with Current on the '='.
    void                          parseExportRename(ExportItem& Item);

    // EP §6.11.3: zero or more 'import' clauses.
    std::vector<ImportClause>     parseImportClauses();

    // import-clause { ',' import-clause }, recorded in Clause.
    void                          parseImportList(ImportClause& Clause);

    // program → 'program' identifier ( '(' identifier-list ')' )? ';' block '.'
    std::unique_ptr<ProgramNode>  parseProgram();

    // block → label-section* const-section* type-section* var-section*
    //         proc-section* compound-stmt
    std::unique_ptr<BlockNode>    parseBlock();

    // label-section → 'label' label-id (',' label-id)* ';'
    // label-id → unsigned-integer | identifier
    void                          parseLabelSection(BlockNode& Block);

    // const-section → 'const' const-def+
    void                          parseConstSection(BlockNode& Block);

    // const-def → identifier '=' simple-expr ';'
    ConstDef                      parseConstDef();

    // type-section → 'type' type-def+
    void                          parseTypeSection(BlockNode& Block);

    // type-def → identifier '=' type-expr ';'
    TypeDef                       parseTypeDef();

    // var-section → 'var' var-group+
    void                          parseVarSection(BlockNode& Block);

    // var-group → identifier-list ':' type-expr ';'
    VarGroup                      parseVarGroup();

    // proc-decl → 'procedure' identifier param-list ';' (block ';' | 'forward' ';')
    // func-decl → 'function'  identifier param-list ':' type-expr ';' (block ';' | 'forward' ';')
    // HeadingOnly is set in a module interface, where the heading stands alone
    // and no body follows it.
    std::unique_ptr<ProcDecl>     parseProcDecl(bool IsFunction,
                                                bool HeadingOnly = false);

    // param-list → ( '(' param-group (';' param-group)* ')' )?
    std::vector<ParamGroup>       parseParamList();

    // param-group → ['var'] identifier-list ':' type-expr
    // 'var' marks pass-by-reference parameters.
    ParamGroup                    parseParamGroup();
    ParamGroup                    parseProcedureParamGroup();

    // statement → assignment | compound-stmt | if-stmt | while-stmt | for-stmt
    //           | repeat-stmt | with-stmt | goto-stmt | labeled-stmt
    //           | call-stmt | ε
    // Returns nullptr for the ε production; callers must handle null.
    std::unique_ptr<StmtNode>     parseStatement();

    // compound-stmt → 'begin' statement (';' statement)* 'end'
    std::unique_ptr<CompoundStmt> parseCompoundStmt();

    // if-stmt → 'if' expression 'then' statement ('else' statement)?
    std::unique_ptr<IfStmt>       parseIfStmt();

    // while-stmt → 'while' expression 'do' statement
    std::unique_ptr<WhileStmt>    parseWhileStmt();

    // for-stmt → 'for' identifier ':=' expression ('to'|'downto') expression 'do' statement
    std::unique_ptr<StmtNode>     parseForStmt(); // may return ForStmt or ForInStmt

    // repeat-stmt → 'repeat' statement (';' statement)* 'until' expression
    std::unique_ptr<RepeatStmt>   parseRepeatStmt();

    // with-stmt → 'with' variable (',' variable)* 'do' statement
    std::unique_ptr<WithStmt>     parseWithStmt();

    // case-stmt → 'case' expression 'of' case-arm (';' case-arm)* [';'] 'end'
    // case-arm  → case-constant {',' case-constant} ':' statement
    std::unique_ptr<CaseStmt>     parseCaseStmt();

    // Parse one write/writeln argument: expression [':' expression [':' expression]]
    // Returns a WriteParam node if width/decimals present, otherwise a plain ExprNode.
    std::unique_ptr<ExprNode>     parseWriteArg();

    // expression → simple-expr ( relop simple-expr )?
    // relop: = <> < <= > >= in
    std::unique_ptr<ExprNode>     parseExpression();

    // simple-expr → ('+' | '-')? term (addop term)*
    // addop: + - or
    std::unique_ptr<ExprNode>     parseSimpleExpr();

    // term → factor (mulop factor)*
    // mulop: * / div mod and
    std::unique_ptr<ExprNode>     parseTerm();
    std::unique_ptr<ExprNode>     parsePower();

    // factor → integer-literal | real-literal | string-literal | 'nil'
    //        | 'true' | 'false'
    //        | 'not' factor
    //        | '(' expression ')'
    //        | '[' set-element-list ']'            ← set literal
    //        | identifier ( '(' expr-list ')' | postfix* )
    // postfix: '[' expression ']' | '.' identifier | '^'
    std::unique_ptr<ExprNode>     parseFactor();

    // case-constant → ('+' | '-')? factor.  ISO Sec6.3: constant = [ sign ]
    // ( unsigned-number | constant-identifier ) | character-string.  Used
    // for a case-statement's labels (Sec6.8.3.5) and a variant-part's
    // (Sec6.4.3.3), both of which are this same production; parseFactor
    // alone has no sign of its own (only parseSimpleExpr, one level up,
    // does), so a bare parseFactor() call left '-1: ...' unparseable in
    // both places. Mirrors parseSubrangeBound's unconditional sign-handling
    // branch, not gated by any dialect option: a signed case-constant is
    // Standard Pascal, not an extension.
    std::unique_ptr<ExprNode>     parseCaseConstant();

    // Applies zero or more postfix operators to Expr: subscript ([]), field
    // access (.), or pointer dereference (^).
    std::unique_ptr<ExprNode>     parsePostfix(std::unique_ptr<ExprNode> Expr);

    // EP §6.8.7: Called when Identifier '[' is seen in EP mode.
    // After consuming the identifier, decides between a structured value
    // constructor (array/record/set) and a plain array index.
    // Consumes the '[' and everything up to the matching ']'.
    // Returns a StructuredValueExpr, SetLiteralExpr, or IndexExpr.
    /// EP §6.8.7.3: appends the arms of a variant-part-value
    ///   'case' [ tag-field-identifier ':' ] constant-tag-value
    ///   'of' '[' field-list-value ']'
    /// to \p Node.  Current is 'case' on entry.
    void parseVariantPartValue(StructuredValueExpr& Node);
    /// EP §6.8.7.3: appends the arms of a field-list-value — the field-values
    /// and any variant-part-value — to \p Node, stopping at the ']'.
    void parseFieldListValue(StructuredValueExpr& Node);

    /// EP §6.6: reads the 'value' clause that may end a type-denoter.
    void                          parseInitialState(TypeNode& Node);
    /// EP §6.8.7.1: an expression, or an array- or record-value written
    /// without the type name that a value constructor carries.
    std::unique_ptr<ExprNode>     parseComponentValue();
    void parseValueLabels(std::vector<std::unique_ptr<ExprNode>>& Out);
    void parseValueArms(StructuredValueExpr& Node);
    std::unique_ptr<ExprNode>     parseStructuredValueOrIndex(std::string Name,
                                                               Token Loc);
};

} // namespace plang
