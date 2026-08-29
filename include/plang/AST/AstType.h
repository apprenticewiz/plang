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
    /// The type-declaration denoter this name was resolved to, from Sema's
    /// symbol table -- i.e. in the scope the name was WRITTEN in.
    ///
    /// CodeGen used to answer that question with a flat `typeAliases` map keyed
    /// by spelling, which has no scope chain, so following a chain of type
    /// names from a foreign declaration re-bound every hop in whatever
    /// procedure was being lowered.  That is how an inner `ca` supplied the
    /// `value` clause -- and its length -- for an outer type's variable, and
    /// memcpy'd 400 bytes into a 4-byte allocation.
    mutable const TypeNode* Denotes{nullptr};
};

/// ISO §6.4.3.2.  The index is written either as a range of bounds, which is
/// what Low and High hold, or as an ordinal type — `array[color]` — which is
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

/// EP §6.4.3.3: variable-length string type  string(N)  --  and, when
/// IsShortString is set, Turbo's bounded string[N] (a distinct type, with a
/// distinct binary layout; see TypeKind::ShortString).  One AST node covers
/// both spellings because they differ only in which bracket the capacity is
/// written inside; Sema and CodeGen branch on IsShortString to resolve/lower
/// each to its own TypeKind.
struct StringTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::StringTypeNode; }
    StringTypeNode() : TypeNode(NodeKind::StringTypeNode) {}
    std::unique_ptr<ExprNode> Capacity; /// the N in string(N) or string[N]; must be a constant expression
    /// True for Turbo's `string[N]`, false for EP's `string(N)`.  The parser
    /// only ever sets this under -std=turbo (see ParseType.cpp); EP's paren
    /// form remains EP's exclusively and Turbo's bracket form remains
    /// Turbo's -- see the resolveType arm that reads this flag for why.
    bool IsShortString{false};
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
    /// Turbo's own `array of T` parameter form (-std=turbo only), reusing
    /// this same node rather than a new TypeKind -- see Type::IsOpenArray's
    /// own comment for the whole design.  Set only by parseTurboOpenArrayParamType;
    /// EP/ISO 7185's conformant-array-schema form (this struct's usual
    /// meaning) never sets it, and the two are gated to opposite dialects,
    /// so nothing downstream ever needs to ask "which form, really" beyond
    /// checking this flag.  Always exactly one dimension: Specs holds one
    /// placeholder entry whose Lo/Hi strings are never read -- Sema
    /// (checkProcBody) and CodeGen (CodeGenProcs.cpp) each synthesize a
    /// PER-PARAMETER-NAME bound-variable name instead (openArrayLowBoundName/
    /// openArrayHighBoundName below), because unlike EP's conformant-array
    /// group semantics, two names sharing one `array of T` group
    /// (`a, b: array of Integer`) have to size INDEPENDENTLY at the call
    /// site -- confirmed empirically against fpc -Mtp.
    bool IsOpenArray{false};
};

/// The names of the hidden, per-parameter-NAME bound variables a Turbo open-
/// array parameter is given -- see ConformantArrayTypeNode::IsOpenArray's own
/// comment for why they must be synthesized per name rather than shared, EP-
/// conformant-array style, across a whole parameter group.  '$' can never
/// appear in a Pascal identifier, so this can never collide with a name the
/// program itself could write.  Sema (registering the symbol) and CodeGen
/// (naming/finding the incoming value) each call these rather than agreeing
/// on the spelling by convention alone.
inline std::string openArrayLowBoundName(const std::string& ParamName) {
    return ParamName + "$low";
}
inline std::string openArrayHighBoundName(const std::string& ParamName) {
    return ParamName + "$high";
}

struct ParamGroup {
    bool                      IsVar{false};       /// true = pass by reference
    bool                      IsProtected{false}; /// EP §6.7.3.1: cannot be assigned inside the body
    /// Turbo's own `const` parameter (-std=turbo only): passed efficiently
    /// (by reference for a structured type -- CodeGenProcs.cpp; by value
    /// otherwise, same as an ordinary value parameter) but, like a
    /// protected EP value parameter, may not be assigned inside the body.
    /// Deliberately a SEPARATE flag from IsProtected rather than folded into
    /// it: the two differ in how CodeGen passes a STRUCTURED actual (const
    /// passes it by reference; protected still copies it in, see
    /// checkProcBody's own comment) and in which diagnostic names the
    /// violation (err_const_param_assigned vs. err_protected_param_assigned/
    /// err_protected_import_assigned) -- a shared flag would need a third
    /// axis threaded through both to tell them apart again.
    bool                      IsConst{false};
    std::vector<std::string>  Names;
    /// Where each name in Names was written, index-aligned with Names.  Used
    /// so diagnostics about one name in a multi-name group ("a, b: integer")
    /// point at that name's own token rather than at the shared type.
    std::vector<SourceLocation> NameLocs;
    /// Where this parameter GROUP itself begins (its 'const'/'var'/protected'
    /// prefix, or its first name when it has none).  Used as the diagnostic
    /// location wherever Type->Loc would ordinarily serve but Type may be
    /// null -- see the field just below.
    SourceLocation             Loc;
    /// Null for a Turbo (-std=turbo only) UNTYPED parameter -- `procedure
    /// P(var x);`, no ': type' at all.  Confirmed against a local fpc -Mtp
    /// check that only the VAR form is legal in real Turbo Pascal (a bare
    /// `procedure P(x);` with no 'var' and no type is rejected outright), so
    /// IsVar is always true whenever Type is null.  Every existing
    /// dereference of this field elsewhere in the compiler was audited and
    /// null-guarded when this was introduced (see the PR that added this
    /// comment for the full list) -- treat a new one exactly as
    /// suspiciously.
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
    /// R3: each actual as a closed form over the ENCLOSING schema's
    /// discriminant indices, when this instantiation is written inside another
    /// schema's body.  `matrix(m,n) = array[1..m] of vector(n)` needs n here,
    /// and n is the outer schema's discriminant -- so the inner schema's
    /// extents are arithmetic over forms rather than over constants.  Empty
    /// when the actuals are ordinary constants.
    mutable std::vector<ExtentForm> ActualForms;
};

} // namespace plang
