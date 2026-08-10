#pragma once

#include "plang/AST/AstExpr.h"  // for ExprNode (used in array bounds and variant case labels)

namespace plang {

// ---------------------------------------------------------------------------
// Type nodes
// ---------------------------------------------------------------------------

struct NamedTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::NamedTypeNode; }
    NamedTypeNode() : TypeNode(NodeKind::NamedTypeNode) {}
    std::string Name;  /// "integer", "real", "boolean", "string", "char", or user type
    /// EP §6.4.2.5: written 'restricted Name', which denotes a type of its own
    /// whose values stand one for one for those of Name and on which no
    /// operation but parameter passing is allowed.
    bool Restricted{false};
};

/// ISO §6.4.3.2.  The index is written either as a range of bounds, which is
/// what Low and High hold, or as an ordinal type — `array[colour]` — which is
/// what Index holds.  Exactly one of the two forms is filled in; the bounds
/// stay the common representation so that the constant-folding paths that read
/// them are unaffected, and only a named index has to go through Sema.
struct ArrayTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ArrayTypeNode; }
    ArrayTypeNode() : TypeNode(NodeKind::ArrayTypeNode) {}
    std::unique_ptr<ExprNode> Low;      /// lower bound of the index range
    std::unique_ptr<ExprNode> High;     /// upper bound of the index range
    std::unique_ptr<TypeNode> Index;    /// index written as an ordinal type
    std::unique_ptr<TypeNode> Element;  /// element type
    bool Packed{false};
};

struct SubrangeTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SubrangeTypeNode; }
    SubrangeTypeNode() : TypeNode(NodeKind::SubrangeTypeNode) {}
    std::unique_ptr<ExprNode> Low;   /// lower bound constant expression
    std::unique_ptr<ExprNode> High;  /// upper bound constant expression
};

struct EnumTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::EnumTypeNode; }
    EnumTypeNode() : TypeNode(NodeKind::EnumTypeNode) {}
    std::vector<std::string> Values;  /// enumerated value names, in declaration order
};

struct FieldDecl {
    /// Note: FieldDecl is NOT a Node subclass; no classof() needed.
    std::vector<std::string>  Names;  /// field names sharing this type
    std::unique_ptr<TypeNode> Type;
};

struct VariantPart;  // forward declaration for use in VariantCase

struct VariantCase {
    std::vector<std::unique_ptr<ExprNode>> Labels;        /// case label constants
    std::vector<FieldDecl>                 Fields;        /// fields in this variant
    std::unique_ptr<VariantPart>           NestedVariant; /// null if no nested case
};

struct VariantPart {
    std::string               TagField;  /// empty if selector is anonymous
    std::unique_ptr<TypeNode> TagType;
    std::vector<VariantCase>  Cases;
};

struct RecordTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::RecordTypeNode; }
    RecordTypeNode() : TypeNode(NodeKind::RecordTypeNode) {}
    std::vector<FieldDecl>       Fields;   /// fixed fields, in declaration order
    std::unique_ptr<VariantPart> Variant;  /// null if no variant part
    bool Packed{false};
};

struct SetTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SetTypeNode; }
    SetTypeNode() : TypeNode(NodeKind::SetTypeNode) {}
    std::unique_ptr<TypeNode> Base;  /// base ordinal type
    bool Packed{false};
};

struct FileTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::FileTypeNode; }
    FileTypeNode() : TypeNode(NodeKind::FileTypeNode) {}
    std::unique_ptr<TypeNode> Element;  /// element type; null for untyped file
    std::unique_ptr<TypeNode> Index;    /// EP §6.4.3.6: direct-access index type; null = sequential
};

struct PackedTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::PackedTypeNode; }
    PackedTypeNode() : TypeNode(NodeKind::PackedTypeNode) {}
    std::unique_ptr<TypeNode> Inner;  /// the packed type
};

struct PointerTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::PointerTypeNode; }
    PointerTypeNode() : TypeNode(NodeKind::PointerTypeNode) {}
    std::unique_ptr<TypeNode> Base;  /// pointed-to type (^base)
};

/// EP §6.4.3.3: variable-length string type  string(N)
struct StringTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::StringTypeNode; }
    StringTypeNode() : TypeNode(NodeKind::StringTypeNode) {}
    std::unique_ptr<ExprNode> Capacity; /// the N in string(N); must be a constant expression
};

/// EP §6.4.9: type of x — evaluates to the static type of a variable.
/// Used as a type-denoter: var y: type of x;  or  procedure p(b: type of a);
struct TypeOfNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::TypeOfNode; }
    TypeOfNode() : TypeNode(NodeKind::TypeOfNode) {}
    std::string VarName; /// the variable whose type is inquired
};

// EP §6.7.3.7: One dimension of a conformant array schema.
// Represents the   lo..hi : OrdType   part of
//   array [lo..hi : OrdType] of ElementType
struct IndexSpec {
    std::string Lo;      // name of the lower-bound variable
    std::string Hi;      // name of the upper-bound variable
    std::string OrdType; // ordinal type name (e.g. "integer")
};

// EP §6.7.3.7: conformant array parameter type node.
// Multi-dimensional abbreviated syntax
//   array [u..v:T1; j..k:T2] of E
// is expanded at parse time to nested ConformantArrayTypeNodes:
//   outer has Specs={u..v:T1}, Element=inner ConformantArrayTypeNode
//   inner has Specs={j..k:T2}, Element=E
struct ConformantArrayTypeNode : TypeNode {
    static bool classof(const Node* n) {
        return n->Kind == NodeKind::ConformantArrayTypeNode;
    }
    ConformantArrayTypeNode() : TypeNode(NodeKind::ConformantArrayTypeNode) {}
    std::vector<IndexSpec>     Specs;   // one entry per dimension (usually 1 after expansion)
    std::unique_ptr<TypeNode>  Element; // element type (may itself be ConformantArrayTypeNode)
    bool Packed{false};
};

struct ParamGroup {
    bool                      IsVar{false};       /// true = pass by reference
    bool                      IsProtected{false}; /// EP §6.7.3.1: cannot be assigned inside the body
    std::vector<std::string>  Names;
    std::unique_ptr<TypeNode> Type;
};

/// ISO §6.6.3.1: a procedural or functional formal parameter, written as the
/// heading of the procedure it will receive —
///   function apply(function f(x: integer): integer; v: integer): integer
///
/// Standard Pascal has no procedure variables, so this appears only as a
/// ParamGroup::Type.  The names inside the inner list are documentation: ISO
/// §6.6.3.6 compares two such headings by structure, never by parameter name.
struct ProcedureTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ProcedureTypeNode; }
    ProcedureTypeNode() : TypeNode(NodeKind::ProcedureTypeNode) {}
    bool                      IsFunction{false};
    std::vector<ParamGroup>   Params;
    std::unique_ptr<TypeNode> ReturnType; /// null for a procedural parameter
};

/// EP §6.4.8: discriminated schema instantiation used as a type-denoter.
/// Example: "Vector(10)" where Vector is a schema type with one discriminant.
/// ResolvedBody is set by Sema::resolveType and consumed by codegen.
struct SchemaTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::SchemaTypeNode; }
    SchemaTypeNode() : TypeNode(NodeKind::SchemaTypeNode) {}
    std::string Name;  ///< schema type name
    std::vector<std::unique_ptr<ExprNode>> Actuals; ///< discriminant value expressions
    /// Cached by Sema (mutable so const TypeNode& can be annotated); used by codegen.
    mutable std::shared_ptr<Type> ResolvedBody;
};

} // namespace plang
