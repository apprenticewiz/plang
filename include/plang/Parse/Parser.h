#pragma once

#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Lex/Scanner.h"
#include "plang/Basic/Token.h"

#include <cstdint>
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

    /// The `{$R+}`-style switch table Lex built up while parse() drove it
    /// through the whole token stream (every include it spliced in along the
    /// way included), or null if none was ever recorded -- forwards to
    /// Scanner::switches(), whose own comment explains why a caller has to
    /// ask for this explicitly rather than see it appear on Opts by itself.
    /// Meaningful only after parse() returns; before that, Lex has not
    /// necessarily read the directive that would have built it yet.
    [[nodiscard]] std::shared_ptr<const SwitchTable> switches() const { return Lex.switches(); }

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

    // parsePower's own right-associative recursion (Node->Right =
    // parsePower(), ParseExpr.cpp) for a '**'/'pow' chain is the one
    // exception to the "every recursive re-entry funnels through parseFactor"
    // claim above (issue #550): it calls parsePower() directly, so
    // ExprDepth/MaxExprDepth never bounds it, and it genuinely deepens the
    // real C++ call stack one frame per chain element with no ceiling at
    // all. Unlike ExprDepth/TypeDepth/StmtDepth/BlockDepth, which each bound
    // a *count* of live activations against a fixed constant, this one is
    // bounded by comparing actual stack-pointer headroom against the
    // platform's real stack budget (see powerStackNearlyExhausted in
    // ParseExpr.cpp) -- a term-count ceiling was tried and reverted (see
    // PR #553/issue #300's reopening comment) because the "right" count is
    // not a fixed number, it depends on how much stack a single parsePower
    // frame actually costs, which varies by build (Debug vs Release) and
    // platform in a way a hardcoded constant cannot track but a live
    // headroom check naturally does.  StackBaseline is captured once, here,
    // at Parser construction (Parser.cpp) as the reference point headroom is
    // measured from; PowerDepth/PowerDepthLimitHit/PowerDepthScope exist only
    // to reset the "already reported" flag between one deep '**' chain and
    // the next in the same file, the same way ExprDepthLimitHit resets above.
    // Unlike ExprDepthScope, whose ceiling can only fire once MaxExprDepth
    // other DepthGuards are already alive on the stack, the headroom check
    // this Guard wraps can fire on the very first activation (PowerDepth ==
    // 0) -- so parsePower constructs its PowerDepthScope unconditionally,
    // before running the check, rather than only when it decides to recurse
    // further; otherwise a first-call exhaustion would latch
    // PowerDepthLimitHit with no Guard ever created to reset it.
    std::uintptr_t            StackBaseline;
    unsigned                  PowerDepth{};
    bool                      PowerDepthLimitHit{};
    struct PowerDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit PowerDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~PowerDepthScope() { if (--N == 0) LimitHit = false; }
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

    // Turbo's parseTurboConstValue (ParseDecl.cpp) is its own single-function
    // recursion -- through a record arm's value and through an array
    // element, both written '(...)' -- separate from EP's own
    // parseComponentValue cycle above (different syntax, different grammar),
    // so it gets its own counter rather than sharing ValueDepth.  Same shape
    // as ValueDepth/ValueDepthScope; see that one's comment for why a
    // ceiling belongs here at all.
    unsigned                 TurboConstValueDepth{};
    bool                     TurboConstValueDepthLimitHit{};
    struct TurboConstValueDepthScope {
        unsigned& N;
        bool&     LimitHit;
        explicit TurboConstValueDepthScope(unsigned& Counter, bool& LimitHitFlag)
            : N(Counter), LimitHit(LimitHitFlag) { ++N; }
        ~TurboConstValueDepthScope() { if (--N == 0) LimitHit = false; }
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

    // Turbo Tier 5, Cluster A item 0 (parsing only -- see ObjectTypeNode's own
    // comment in AstDecl.h for the full grammar and every field-practice
    // decision this was checked against):
    //   object-type → 'object' [ '(' ancestor-type-name ')' ]
    //                 object-member-list 'end'
    // Called with Current pointing at 'object'.  -std=turbo only.
    std::unique_ptr<ObjectTypeNode> parseObjectType();

    // One IN-CLASS method heading inside an object-type's member list --
    // 'constructor'/'destructor'/'procedure'/'function' name param-list
    // [':' result-type] ';' ['virtual' ';'] ['abstract' ';'].  Always
    // heading-only (IsForward left true, Body left null): see ObjectMember's
    // own comment for why this reuses ProcDecl the same way a unit
    // interface's HeadingsOnly declarations already do.
    std::unique_ptr<ProcDecl> parseObjectMethodHeading();

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

    // Turbo's own open-array parameter form: array of T -- no bracket, no
    // bound-variable clause at all (-std=turbo only; called from
    // parseParamGroup instead of parseConformantOrRegular when this dialect
    // is active).  'array' has not yet been consumed.
    std::unique_ptr<TypeNode> parseTurboOpenArrayParamType();

    // variant-part → 'case' [identifier ':'] type-expr 'of' variant-case*
    // Called with Current pointing at 'case'.
    std::unique_ptr<VariantPart>    parseVariantPart();

    // ---------------------------------------------------------------------------
    // Grammar rules — one method per non-terminal
    // ---------------------------------------------------------------------------

    // EP §6.11: file may begin with module definitions before the program.
    // Collects all module nodes then delegates to parseProgram().
    std::unique_ptr<ProgramNode>  parseMultiUnitFile();

    // Turbo Tier 4: a standalone `unit Name; interface ... implementation
    // ... end.` file -- no 'program' anywhere in it.  Returns a ProgramNode
    // whose BareUnit carries the real content; see BareUnit's own comment
    // in AstDecl.h for why the return type stayed ProgramNode.
    std::unique_ptr<ProgramNode>  parseUnitFile();

    // The declaration sections of one unit section (interface or
    // implementation), in Turbo's always-free order (LangFeatures.def's
    // FreeDeclarationOrder, which -std=turbo already sets on every ordinary
    // block via parseBlock -- this is the unit-section equivalent, modeled
    // on parseModuleDeclarations just above but kept separate rather than
    // shared, the same way UnitNode itself is kept separate from ModuleNode).
    // HeadingsOnly is set for the interface section, where a procedure or
    // function is written as its heading alone.
    std::unique_ptr<BlockNode>    parseUnitDeclarations(bool HeadingsOnly);

    // Turbo's own 'uses' clause: 'uses' identifier (',' identifier)* ';'.
    // No qualified/only/rename syntax at all -- contrast EP's import above.
    // Current must be on 'uses'; consumes through the trailing ';'.
    std::vector<UsedUnit>         parseUsesClause();

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
    //           | identifier ':' type-expr '=' turbo-const-value ';'   (-std=turbo)
    ConstDef                      parseConstDef();

    // Turbo typed-constant value: a plain expression, or a parenthesized
    // aggregate -- '(' value (',' value)* ')' for an array (purely
    // positional -- Turbo has no EP-style index label), or
    // '(' identifier ':' value (';' identifier ':' value)* ')' for a record.
    // Which of the two a '(' begins is not known from the declared type (the
    // parser tracks no shape for a named type), so it is read the way EP's
    // own structured-value-constructor grammar is (parseComponentValue,
    // ParseInit.cpp): generically, deferring the real type-directed
    // interpretation to Sema's checkStructuredValue.  Reused here as the same
    // StructuredValueExpr node EP's constructors already build -- an array
    // arm carries no label at all (EP's always does; checkStructuredValue's
    // array case already tolerates an empty label list -- it only checks
    // labels that are there -- so no Sema change was needed to type-check
    // this positional form), a record arm's one label is the field name.
    // CodeGen's own lowering for a typed constant is new regardless (see
    // buildTypedConstInit, CGTypedConst.cpp): it folds straight to a
    // compile-time llvm::Constant rather than the runtime store/GEP/memcpy
    // sequence EP's own emitStructuredValue builds.
    std::unique_ptr<ExprNode>     parseTurboConstValue();

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

    // -std=turbo: SizeOf/High/Low's sole argument, which -- uniquely among
    // every builtin call this parser handles -- may be a TYPE NAME rather
    // than a value expression.  A user-defined type name is an ordinary
    // identifier already (parsed as an IdentExpr exactly like a variable
    // reference; Sema tells the two apart by what the name resolves to),
    // but the five PRIMITIVE type names -- integer, real, boolean, char,
    // string -- are lexer KEYWORDS, not identifiers, and parseFactor's
    // `default:` arm has no case for any of them: `SizeOf(Integer)` would
    // otherwise fail to parse at all.  Deliberately scoped to exactly the
    // first argument of these three spellings -- called only from the
    // CallExpr argument-parsing site in parseFactor, and only for its FIRST
    // argument -- so that a keyword type name is admitted nowhere else:
    // `x := Integer + 1` must keep failing to parse.
    std::unique_ptr<ExprNode>     parseSizeHighLowArg(const std::string& Callee);

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
