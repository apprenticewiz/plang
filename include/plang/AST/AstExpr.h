#pragma once

#include "plang/Basic/BuiltinIDs.h"

#include "plang/AST/AstBase.h"
#include <cstdint>

namespace plang {

// ---------------------------------------------------------------------------
// Expression nodes
// ---------------------------------------------------------------------------

struct IntLitExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IntLitExpr; }
    IntLitExpr() : ExprNode(NodeKind::IntLitExpr) {}
    int64_t Value{0};          /// numeric value of the integer literal
};

struct RealLitExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::RealLitExpr; }
    RealLitExpr() : ExprNode(NodeKind::RealLitExpr) {}
    double Value{0.0};         /// numeric value of the real literal
};

struct StringLitExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::StringLitExpr; }
    StringLitExpr() : ExprNode(NodeKind::StringLitExpr) {}
    std::string Value;         /// string contents with escape sequences resolved
};

struct BoolLitExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::BoolLitExpr; }
    BoolLitExpr() : ExprNode(NodeKind::BoolLitExpr) {}
    bool Value{false};         /// true for 'true', false for 'false'
};

struct NilExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::NilExpr; }
    NilExpr() : ExprNode(NodeKind::NilExpr) {}
    /// the nil pointer constant; no additional data
};

struct IdentExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IdentExpr; }
    IdentExpr() : ExprNode(NodeKind::IdentExpr) {}
    std::string Name;          /// identifier as written in the source (original casing)

    /// R2: Sema found a declaration of this name, in the scope the name was
    /// WRITTEN in.  ISO §6.2.2.10 gives the required identifiers -- eof, eoln,
    /// integer, maxint -- to a region enclosing the program, so a program that
    /// declares one of those names means its own, and codegen must not answer
    /// from its own idea of what the name means.
    ///
    /// A flag and not the `const Symbol*` the plan first called for.  Symbols
    /// live in SymbolTable::Scopes, a std::vector<Scope> whose popScope() pops
    /// the vector and destroys every Symbol in that scope: a pointer stored
    /// here would dangle the moment the declaring block closed.  That is the
    /// same defect as the raw Type* in ~TypeContext, and worth not writing
    /// twice.  Anything more than "was it declared" should be recorded as a
    /// value too, or the symbol table given stable storage first.
    mutable bool UserDeclared{false};

    /// Narrower than UserDeclared: true only when the nearer declaration
    /// found is specifically a procedure/function (SymbolKind::Proc), not
    /// any user declaration at all. UserDeclared alone is too broad a signal
    /// for "this name should be resolved as a call, not read as a constant"
    /// -- an ordinary enum literal or named constant is also UserDeclared,
    /// and treating those as "shadowed" broke their normal constant-table
    /// resolution (issue #129's own fix regressed this before it shipped;
    /// caught by an independent full-suite verification pass, not the
    /// fixing agent's own testing).
    mutable bool UserDeclaredCallable{false};

    /// True only when Sema::resolveTypeArgOrValue (SizeOf/High/Low/TypeOf's
    /// shared dual-shape argument resolver) resolved this occurrence as a
    /// bare TYPE NAME (SizeOf(Byte), TypeOf(TDog)) rather than as an
    /// ordinary value expression -- set directly by that function, both for
    /// one of the five primitive-type keywords and for a user TypeAlias
    /// symbol, the two cases where it returns ResolvedType without ever
    /// calling checkExpr on this node. Neither UserDeclared nor
    /// UserDeclaredCallable can stand in for this: UserDeclared is equally
    /// true for a plain variable of the same syntactic shape, and Pascal
    /// gives types and variables one shared namespace per scope, so nothing
    /// about the identifier itself (only which kind of symbol Sema's lookup
    /// actually found) says which this is. TypeOf's own codegen
    /// (CGFuncCall.cpp) needs this to know whether Args[0] has a runtime
    /// value at all to read a `_vptr` from (a value expression) or not (a
    /// type name) -- issue #508's own bug was CodeGen answering statically
    /// for EVERY shape; the fix needed a reliable way to keep answering
    /// statically for just this one, where no runtime instance exists.
    mutable bool IsTypeArgument{false};

    /// True only when Sema::checkTypeCast resolved this occurrence as the
    /// OPERAND of a Turbo untyped-var-parameter typecast (`Integer(x)` where
    /// `x: var` has no declared type) -- set directly by that function's own
    /// special case, which also aliases this node's ResolvedType to the
    /// cast's TARGET type so the write-side (isLValue's TypeCastExpr case)
    /// sees a trivial same-size match. That alias is exactly what makes the
    /// READ side ambiguous purely from ResolvedType: CGExprCore::
    /// emitTypeCastValue's bothScalar test (Dst ordinal-or-real AND Src
    /// ordinal-or-real) spuriously reads true for ANY ordinal/real target,
    /// since Src now IS Dst, sending it down the "genuine value conversion"
    /// path (emitExpr(x), i.e. an ordinary load through x's OWN storage
    /// type) instead of the correct "reinterpret x's REFERENT" path
    /// (emitLValue(x) -- x's storage IS the caller's address for an untyped
    /// var param -- loaded back at the target's width). This flag is the
    /// one syntactic fact that survives the alias, so emitTypeCastValue
    /// checks it first and never reaches the ambiguous bothScalar test at
    /// all for this shape (issue #645).
    mutable bool IsUntypedParamCastOperand{false};

    /// What a bare occurrence of an ENCLOSING FUNCTION'S OWN NAME (or its
    /// EP §6.7.2 named result variable) means, decided once by
    /// Sema::checkIdent and read back by codegen instead of re-deriving it
    /// from `toLower(Name) == toLower(CurFuncName)` -- CodeGen has no
    /// syntactic-position information of its own (whether THIS occurrence
    /// was the direct target of an assignment or an ordinary read), and a
    /// second, independent name comparison could only ever re-derive the
    /// OLD, dialect-blind rule, not this one.  Meaningless (stays Ordinary)
    /// for every other identifier.
    enum class IdentResolution {
        /// Not (or not yet) inside any enclosing function's own name.
        Ordinary,
        /// ISO §6.8.2.2's assignment-statement carve-out: 'name := expr'
        /// (or 'name.field := expr' / 'name[i] := expr', the root of a
        /// longer target path) inside the function it names.  Reads the
        /// result cell in place; unaffected by -std=turbo.
        ResultVariable,
        /// Every OTHER bare occurrence of the enclosing function's own name
        /// -- ISO §6.7.3's function-designator, called with zero actual
        /// parameters.  Turbo only for now (see checkIdent's own comment);
        /// ISO 7185/Extended Pascal keep ResultVariable here too, matching
        /// this project's behaviour before this enum existed.
        RecursiveCall,
        /// Turbo procedural VALUES: this identifier names a declared
        /// procedure/function (SymbolKind::Proc) and stands for the ROUTINE
        /// ITSELF -- a flat function-pointer value to store in or compare
        /// against a procedural variable -- rather than for a call to it.
        /// Set by Sema::checkRoutineValue, which decides this from the
        /// SYNTACTIC context the identifier was found in (the direct operand
        /// of `@`, or the direct RHS of an assignment whose target's type is
        /// itself callable) -- never from the identifier alone, so this does
        /// not disturb the ubiquitous "FuncName := ..." result-assignment
        /// idiom or ISO §6.7.3's implicit zero-argument call for a bare
        /// function name used any other way.  Read by codegen (CGExprCore's
        /// emitExpr) BEFORE its ordinary "a bare function identifier is a
        /// call" dispatch, so a routine reference is never accidentally
        /// invoked instead of taken.
        RoutineReference,
        /// Turbo procedural VALUES (issue #649): this identifier names a
        /// procedural-typed VARIABLE (SymbolKind::Var/VarParam -- an
        /// ordinary declaration, never SymbolKind::Proc; RoutineReference
        /// above is that case) and this occurrence wants its own STORED
        /// value read, not an implicit call through it. checkIdent's
        /// default for a bare, FUNCTION-typed procedural variable is now to
        /// auto-call it, exactly like a plain function's own bare name
        /// already does (ISO Sec6.7.3) -- fpc -Mtp does too, and refusing to
        /// made 'writeln(fn)' and 'writeln(G)' read the same syntax two
        /// different ways depending only on whether G's value had been
        /// materialized into a procedural variable first. This is the one
        /// carve-out: set (mirroring RoutineReference's identical carve-out
        /// for a bare routine NAME, just above) by the same few syntactic
        /// positions that already special-case RoutineReference -- the
        /// direct RHS of an assignment whose target's type is itself
        /// callable (checkAssign), the direct operand of `@` (checkUnary)
        /// -- plus Assigned's own argument (checkCallExpr/checkCallStmt),
        /// which unlike those two has no destination type of its own to
        /// disambiguate by and so always wants the raw value. Read by
        /// codegen (CGExprCore's emitExpr) to skip the "a bare
        /// function-typed procedural variable auto-calls" rule it
        /// otherwise applies by default -- see checkIdent's own comment.
        ProcVarRawValue,
        /// Issue #773: this bare identifier names no ordinary declaration at
        /// all, but DOES match a parameterless FUNCTION method of the
        /// currently active implicit receiver (Sema::findImplicitCallMethod
        /// -- the same lookup checkImplicitMethodCallExpr already uses for
        /// the parenthesized 'Area()' spelling, reached here instead for the
        /// bare 'Area' one). ImplicitMethodReceiverType below names which
        /// receiver matched; codegen reads both to emit the identical
        /// emitBoundMethodCall call CGFuncCall::emitCallExpr's own
        /// ImplicitMethodReceiverType branch already emits for the
        /// parenthesized form.
        ImplicitMethodCall,
    };
    mutable IdentResolution Resolution{IdentResolution::Ordinary};

    /// Set by Sema::checkImplicitMethodIdent exactly when Resolution is
    /// ImplicitMethodCall -- the same active receiver Type that matched,
    /// mirroring CallExpr::ImplicitMethodReceiverType's identical field
    /// (AstExpr.h's own CallExpr, below) for the parenthesized spelling.
    mutable std::shared_ptr<Type> ImplicitMethodReceiverType;
};

// IndexExpr/FieldExpr/DerefExpr/BinaryExpr/SubstringExpr/MethodCallExpr below
// each declare (rather than implicitly default) their own destructor -- see
// AstExprDestroy.cpp for why and for the shared iterative teardown routine
// all six of them call into.
//
// Short version: each of these owns another ExprNode through exactly the
// field a long, flat chain of the same construct threads through --
// IndexExpr::Array for 'x[1][1]...[1]', FieldExpr::Record for
// 'x.a.a.a...a', DerefExpr::Pointer for 'x^^^...^' (all three built by
// Parser::parsePostfix's iterative loop), BinaryExpr::Left for
// 'x := 1+1+...+1' (parseSimpleExpr/parseTerm's addop/mulop loops) and
// BinaryExpr::Right for a '**' chain (parsePower's own right recursion --
// see issue #550 and StackBaseline's comment in Parser.h for that half of
// this), SubstringExpr::Str and MethodCallExpr::Receiver (also
// parsePostfix). None of that parsing is itself recursive C++ call depth
// (the loops are iterative, or -- parsePower's case -- separately bounded),
// but the AST it builds is exactly as deep as the input asked for, and the
// COMPILER-GENERATED destructor these six would otherwise get is
// unavoidably recursive: destroying the outer node destroys its owned
// child, which (being the same node kind, one link further down the chain)
// destroys ITS owned child, and so on -- one real C++ stack frame per link,
// with no guard anywhere in that path, because there is no per-activation
// RAII scope to hang a depth counter on the way ExprDepth/TypeDepth/
// StmtDepth/BlockDepth hang one on a recursive *function*.  A chain long
// enough to build (parsePostfix and the addop/mulop loops accept input of
// any length) is exactly long enough to blow the stack tearing back down,
// regardless of whether Sema went on to accept or correctly reject the
// expression -- issue #551's own repro crashes on teardown even after
// Sema's MaxExprDepth=1000 has already cleanly rejected it. UnaryExpr and
// TypeCastExpr were checked too (Operand is the same single-child shape)
// but do NOT need this: both recurse only through parseFactor, which
// Parser::ExprDepth already caps at 500 activations, so neither can ever be
// built more than 500 deep in the first place -- nowhere near a real
// concern for the compiler-generated destructor.  CallExpr/SetLiteralExpr/
// StructuredValueExpr and the like are not chain-shaped at all: they hold a
// std::vector of children, whose own destructor is already an ordinary loop
// (not per-element recursion) over each element, and any one element that
// were itself one of the six iterative types below is already made safe by
// that type's own fix.
struct IndexExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IndexExpr; }
    IndexExpr() : ExprNode(NodeKind::IndexExpr) {}
    ~IndexExpr();
    std::unique_ptr<ExprNode> Array;  /// the array being subscripted
    std::unique_ptr<ExprNode> Index;  /// the subscript expression
};

struct FieldExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::FieldExpr; }
    FieldExpr() : ExprNode(NodeKind::FieldExpr) {}
    ~FieldExpr();
    std::unique_ptr<ExprNode> Record;  /// the record being accessed
    std::string               Field;   /// name of the field

    /// Issue #773: set by Sema::checkField exactly when \c Field named no
    /// actual field of Record's own Object type, but DID match a
    /// parameterless FUNCTION method on it (Sema::findObjectMethod, the
    /// same ancestor-chain walk checkMethodCall itself uses for the
    /// parenthesized 'S.Area()' spelling) -- 'S.Area' with no parentheses,
    /// found from OUTSIDE the object's own methods, where there is no
    /// active implicit receiver for checkImplicitMethodIdent's stack-based
    /// lookup to consult; Record's own already-resolved Object type supplies
    /// the receiver directly instead. Read by CGFieldAccess::emitFieldLoad
    /// to emit a bound method call (CGFuncCall::emitBoundMethodCall) rather
    /// than an ordinary field GEP+load, and by Sema::isLValue to refuse
    /// 'S.Area := ...' as an assignment target -- a function's result is not
    /// itself a variable to write back through.
    mutable bool IsImplicitMethodCall{false};
};

struct DerefExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::DerefExpr; }
    DerefExpr() : ExprNode(NodeKind::DerefExpr) {}
    ~DerefExpr();
    std::unique_ptr<ExprNode> Pointer;  /// the pointer being dereferenced (p^)
};

struct BinaryExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::BinaryExpr; }
    BinaryExpr() : ExprNode(NodeKind::BinaryExpr) {}
    ~BinaryExpr();
    TokenKind                 Op{TokenKind::Eof};  /// operator token
    std::unique_ptr<ExprNode> Left, Right;         /// operands
};

struct UnaryExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::UnaryExpr; }
    UnaryExpr() : ExprNode(NodeKind::UnaryExpr) {}
    TokenKind                 Op{TokenKind::Eof};  /// Minus/Plus, Not, or At (Turbo `@x` address-of)
    std::unique_ptr<ExprNode> Operand;
};

struct CallExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::CallExpr; }
    CallExpr() : ExprNode(NodeKind::CallExpr) {}
    std::string                            Name;  /// function name
    std::vector<std::unique_ptr<ExprNode>> Args;  /// actual arguments, in order

    /// Which required function Sema resolved this call to, or None where it
    /// resolved to one the program declared.  ISO §6.2.2.10 lets a program
    /// declare its own `abs` or `round`, and the name alone cannot say which
    /// was meant — only the scope the call was written in can, and Sema is
    /// what knows it.  Carrying the identity rather than a flag means what
    /// Sema decided is what codegen acts on, without matching the spelling a
    /// second time against a second list.
    /// Mutable so that Sema can annotate through a const reference, as it does
    /// for TypeNode::ResolvedType.
    mutable BuiltinID ResolvedBuiltin{BuiltinID::None};

    /// Turbo Tier 5, issues #571/#623: CallStmt::ImplicitMethodReceiverType's
    /// own expression-context mirror, set by
    /// Sema::checkImplicitMethodCallExpr -- see that field's own comment
    /// (AstStmt.h) for the whole design.  Null for an ordinary call.
    mutable std::shared_ptr<Type> ImplicitMethodReceiverType;
};

struct SetRangeExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SetRangeExpr; }
    SetRangeExpr() : ExprNode(NodeKind::SetRangeExpr) {}
    std::unique_ptr<ExprNode> Low;   /// lower bound of a range in a set literal
    std::unique_ptr<ExprNode> High;  /// upper bound
};

struct SetLiteralExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SetLiteralExpr; }
    SetLiteralExpr() : ExprNode(NodeKind::SetLiteralExpr) {}
    /// Elements are ExprNode (single value) or SetRangeExpr (lo..hi range).
    std::vector<std::unique_ptr<ExprNode>> Elements;
    /// EP §6.8.7.4: optional type-name prefix ("" = untyped [] literal).
    std::string TypeName;
};

/// EP §6.5.6: substring variable s[i..j] — an lvalue into a string(N).
struct SubstringExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SubstringExpr; }
    SubstringExpr() : ExprNode(NodeKind::SubstringExpr) {}
    ~SubstringExpr();
    std::unique_ptr<ExprNode> Str;   /// the string variable
    std::unique_ptr<ExprNode> Low;   /// 1-based start index (inclusive)
    std::unique_ptr<ExprNode> High;  /// 1-based end index (inclusive)
};

/// EP §6.8.7: one arm in a typed value constructor.
/// - Array constructor arm: Labels = index/range expressions; Value = the element value.
/// - Record constructor arm: Labels = IdentExpr nodes (field names); Value = the field value.
/// - Set constructor element: Labels = element expression(s); Value = null.
/// - 'otherwise' arm: IsOtherwise = true; Labels = empty; Value = the default value.
struct StructuredValueArm {
    bool                                   IsOtherwise{false};
    std::vector<std::unique_ptr<ExprNode>> Labels;  ///< indices, field names, or set elements
    std::unique_ptr<ExprNode>              Value;   ///< null for set elements
};

/// EP §6.8.7: TypeName '[' arm { ';' arm } ']'
/// Used for array constructors (§6.8.7.2), record constructors (§6.8.7.3),
/// and typed set constructors (§6.8.7.4).  Sema resolves the constructor kind
/// from TypeName and sets ResolvedType accordingly.
struct StructuredValueExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::StructuredValueExpr; }
    StructuredValueExpr() : ExprNode(NodeKind::StructuredValueExpr) {}
    std::string TypeName;                     ///< name of the array/record/set type
    std::vector<StructuredValueArm> Arms;     ///< constructor arms in source order
};

/// Turbo Pascal typecast: TypeName '(' expr ')'.
///
/// Two different things depending on how it is used, both spelled the same
/// way. As a VALUE (Integer(SomeReal)) it converts: the ordinal or real
/// value is truncated, rounded, or reinterpreted per Turbo's own conversion
/// rules, and the result is a value like any other expression's. As a
/// VARIABLE (TByteRec(SomeWord).Lo := 0, an lvalue) it does not convert
/// anything -- it reinterprets the OPERAND'S OWN STORAGE in place, so a
/// write through it mutates the operand's storage directly.
///
/// This is its own NodeKind, not a CallExpr with a type-named callee,
/// specifically because of the lvalue form: CGExprCore::emitLValue's
/// fallback for a CallExpr spills the call's result to a fresh temporary,
/// which would silently turn a variable typecast into a copy -- exactly the
/// bug a distinct NodeKind lets emitLValue avoid by giving it its own case
/// that hands back the operand's own pointer instead.
///
/// A type name and a routine name can never share a scope in Pascal, so the
/// parser recognizes this shape (Parser::TypeNames_) at parse time rather
/// than leaving CallExpr-vs-TypeCastExpr for Sema to sort out after the
/// fact -- see parseFactor's and parseStatement's identifier branches.
struct TypeCastExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::TypeCastExpr; }
    TypeCastExpr() : ExprNode(NodeKind::TypeCastExpr) {}
    std::string                TypeName;  /// target type name, as written
    std::unique_ptr<ExprNode>  Operand;   /// the expression being cast
};

/// A write/writeln argument with optional field-width and decimal specifiers.
/// write(e : width : decimals) — ISO §6.9.3
struct WriteParam : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::WriteParam; }
    WriteParam() : ExprNode(NodeKind::WriteParam) {}
    std::unique_ptr<ExprNode> Value;     /// the expression being written
    std::unique_ptr<ExprNode> Width;     /// field width; null if not specified
    std::unique_ptr<ExprNode> Decimals;  /// decimal places; null if not specified (reals only)
};

/// Turbo Tier 5, Cluster A item 3: a receiver-carrying method call --
/// `Obj.Method(args)` or `P^.Method(args)` -- used as a VALUE (a function
/// method called where an expression is expected).  See MethodCallStmt
/// (AstStmt.h) for the statement-context sibling (a procedure method, or a
/// function method called with its result discarded under Turbo's default
/// `{$X+}`).
///
/// Parsed by Parser::parsePostfix exactly where FieldExpr is: `.identifier`
/// immediately followed by `(` builds this node instead of a FieldExpr,
/// with Receiver holding whatever postfix chain came before the dot (a
/// plain IdentExpr for `Obj.Method(...)`, or a DerefExpr for `P^.Method(...)`
/// -- confirmed against a local fpc -Mtp build that the `^` may not be
/// omitted: `P.Method` for P: ^TAnimal is "Illegal qualifier", so a bare
/// pointer receiver never reaches Sema with Object type at all and the
/// ordinary err_method_call_receiver_not_object diagnostic covers it).
///
/// The parser cannot tell a genuine method call from a field access
/// followed by a parenthesized sub-expression that just happens to sit next
/// to it (there is no such thing in this codebase -- no procedural-typed
/// record field exists to call -- but the parser does not know that; it
/// only knows the two token shapes are otherwise identical), so this is
/// built unconditionally whenever `.identifier(` is seen and Sema
/// (Sema::checkMethodCall) is what actually confirms Receiver's type is
/// TypeKind::Object and Method names something in its VMT-slot ancestor
/// chain, reporting err_method_call_receiver_not_object /
/// err_object_method_not_found otherwise.
struct MethodCallExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::MethodCallExpr; }
    MethodCallExpr() : ExprNode(NodeKind::MethodCallExpr) {}
    ~MethodCallExpr();
    std::unique_ptr<ExprNode>              Receiver;  /// object-typed expression (or P^ deref of one)
    std::string                            Method;    /// method name, as written
    std::vector<std::unique_ptr<ExprNode>> Args;       /// actual arguments, in order
};

/// Turbo Tier 5, issue #509: 'inherited [Method[(args)]]' used as a VALUE --
/// the expression-context sibling of InheritedCallStmt (AstStmt.h), exactly
/// the way MethodCallExpr is MethodCallStmt's own sibling just above.  Same
/// design throughout: a STATIC call (never through the VMT) to the DIRECT
/// PARENT's own implementation of a method with the given name, resolved by
/// Sema::checkInheritedCall (shared with checkInheritedCallStmt) and emitted
/// by CGFuncCall::emitInheritedCallExpr (shared design with CGProcCall::
/// emitInheritedCallStmt, just returning the call's value instead of
/// discarding it). See InheritedCallStmt's own comment for the whole
/// design, including the two surface forms (explicit 'inherited Method(...)'
/// vs. bare 'inherited' forwarding this activation's own arguments) -- both
/// apply identically here.
///
/// Deliberately has no ResolvedType field of its own: unlike
/// InheritedCallStmt (a StmtNode, which has no such field to begin with),
/// this is an ExprNode, and ExprNode::ResolvedType already carries Sema's
/// answer generically -- the same reason MethodCallExpr does not repeat
/// MethodCallStmt::ResolvedType either.
struct InheritedCallExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::InheritedCallExpr; }
    InheritedCallExpr() : ExprNode(NodeKind::InheritedCallExpr) {}
    /// The method name as written, or empty for the bare 'inherited' form.
    std::string                            Method;
    /// Actual arguments, in order; always empty for the bare form -- see
    /// InheritedCallStmt::Args's own comment.
    std::vector<std::unique_ptr<ExprNode>> Args;

    /// Sema::checkInheritedCall's resolution, consumed by
    /// CGFuncCall::emitInheritedCallExpr -- mirrors InheritedCallStmt's own
    /// four fields of the same names exactly; see their comments there.
    mutable std::string ResolvedMethod;
    mutable std::string ImplementingType;
    mutable std::string ImplementingModule;
    /// Turbo Tier 5 issue #682: see InheritedCallStmt::ImplementingOwnerType's
    /// own comment (AstStmt.h) -- identical purpose and lifetime, just for
    /// the expression form.
    mutable std::shared_ptr<Type> ImplementingOwnerType;
};

/// Turbo procedural VALUES (issue #648): a call reached through an
/// arbitrary procedural-typed EXPRESSION -- 'a[i](args)' (an array
/// element), 'p^(args)' (a dereferenced pointer to a procedural value) --
/// rather than through a bare NAME (CallExpr, resolved by spelling
/// against the symbol table) or a genuine object method (MethodCallExpr,
/// receiver + method name).  Built by Parser::parsePostfix whenever '('
/// follows a postfix chain that is not itself a bare IdentExpr (still
/// built as an ordinary CallExpr one token earlier, before postfix
/// chaining even starts -- parseFactor's own Identifier arm) and not a
/// '.identifier(' (built as MethodCallExpr instead, by this same
/// function's own Dot case).  Sema (Sema::checkIndirectCall) confirms
/// Callee's own resolved type is itself callable (Procedure/Function) the
/// same way checkUserDefinedCall's procedural-VARIABLE arm already does
/// for a bare name, and reports err_not_callable otherwise; CodeGen
/// (ClosureAndCallABI::emitIndirectCall) reads Callee's address the same
/// way every other indirect access already does (emitLValue) and builds
/// its own LLVM call signature straight from that resolved Type, since
/// there is no single AST ProcedureTypeNode this callee's type was
/// declared with in general.
///
/// One of the six/seven "chain-shaped" node kinds a long postfix
/// expression ('f()()()...()') can build arbitrarily deep through
/// Parser::parsePostfix's own iterative loop -- AstExprDestroy.cpp tears
/// this down the same non-recursive way it already does for
/// IndexExpr/FieldExpr/DerefExpr/BinaryExpr/SubstringExpr/MethodCallExpr,
/// through its own Callee field, for the identical reason (issue #551).
struct IndirectCallExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IndirectCallExpr; }
    IndirectCallExpr() : ExprNode(NodeKind::IndirectCallExpr) {}
    ~IndirectCallExpr();
    std::unique_ptr<ExprNode>              Callee;  /// procedural-typed expression called through
    std::vector<std::unique_ptr<ExprNode>> Args;    /// actual arguments, in order
};

} // namespace plang
