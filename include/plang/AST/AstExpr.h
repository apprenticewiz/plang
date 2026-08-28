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
    };
    mutable IdentResolution Resolution{IdentResolution::Ordinary};
};

struct IndexExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IndexExpr; }
    IndexExpr() : ExprNode(NodeKind::IndexExpr) {}
    std::unique_ptr<ExprNode> Array;  /// the array being subscripted
    std::unique_ptr<ExprNode> Index;  /// the subscript expression
};

struct FieldExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::FieldExpr; }
    FieldExpr() : ExprNode(NodeKind::FieldExpr) {}
    std::unique_ptr<ExprNode> Record;  /// the record being accessed
    std::string               Field;   /// name of the field
};

struct DerefExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::DerefExpr; }
    DerefExpr() : ExprNode(NodeKind::DerefExpr) {}
    std::unique_ptr<ExprNode> Pointer;  /// the pointer being dereferenced (p^)
};

struct BinaryExpr : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::BinaryExpr; }
    BinaryExpr() : ExprNode(NodeKind::BinaryExpr) {}
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

/// A write/writeln argument with optional field-width and decimal specifiers.
/// write(e : width : decimals) — ISO §6.9.3
struct WriteParam : ExprNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::WriteParam; }
    WriteParam() : ExprNode(NodeKind::WriteParam) {}
    std::unique_ptr<ExprNode> Value;     /// the expression being written
    std::unique_ptr<ExprNode> Width;     /// field width; null if not specified
    std::unique_ptr<ExprNode> Decimals;  /// decimal places; null if not specified (reals only)
};

} // namespace plang
