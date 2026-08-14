#pragma once

#include "plang/Basic/Token.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace plang {

/// EP §6.4.7 R3: a schema body's extent, as arithmetic over the schema's
/// discriminants BY INDEX, with every other leaf already folded to a value in
/// the scope the declaration was written in.
///
/// The point is what it does NOT contain: an identifier.  CodeGen used to
/// re-emit the declaration's extent EXPRESSIONS wherever an object was
/// allocated or accessed, which resolved their names in the procedure doing
/// the allocating -- so a `const k` used in a bound was captured by any
/// unrelated `var k` there, and the object was sized from a run-time variable.
/// That shipped in 0.1.5, corrupted the heap, and 0.1.6 guarded it by hiding
/// the caller's scope.  A closed form makes it impossible instead: there is no
/// name left to resolve in the wrong room.
struct ExtentForm {
    enum class Op : uint8_t { Const, Disc, Add, Sub, Mul, Div, Mod, Pow, Neg };
    Op                      Kind{Op::Const};
    /// Const: the value.  Disc: the discriminant's index.
    int64_t                 Value{0};
    std::vector<ExtentForm> Args;
};

struct Type; // forward declaration — complete definition in plang/Sema/Type.h

// ---------------------------------------------------------------------------
// NodeKind — discriminator for LLVM-style RTTI
//
/// Every concrete Node subclass carries one of these values so that
/// llvm::isa<> / llvm::dyn_cast<> work without C++ RTTI (-fno-rtti).
/// Abstract base classes (ExprNode, StmtNode, TypeNode) are identified by
/// a contiguous range [XxxFirst, XxxLast] checked in their classof().
// ---------------------------------------------------------------------------
enum class NodeKind {
    // -- Expressions --
    ExprFirst,
    IntLitExpr    = ExprFirst,
    RealLitExpr,
    StringLitExpr,
    BoolLitExpr,
    NilExpr,
    IdentExpr,
    IndexExpr,
    FieldExpr,
    DerefExpr,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    SetRangeExpr,
    SetLiteralExpr,
    SubstringExpr,        // EP §6.5.6: s[i..j] substring variable
    StructuredValueExpr,  // EP §6.8.7: typed value constructor (array/record/set)
    ExprLast      = StructuredValueExpr,

    // write/writeln argument wrapper: appears only in write/writeln arg lists.
    // Stored as ExprNode* via C++ inheritance but excluded from ExprFirst..ExprLast
    // so generic expression traversals don't treat it as a first-class expression.
    WriteParam,

    // -- Statements --
    StmtFirst,
    AssignStmt    = StmtFirst,
    CompoundStmt,
    IfStmt,
    WhileStmt,
    ForStmt,
    ForInStmt,  // EP §6.9.3.9.3: for v in set-expr do
    RepeatStmt,
    CallStmt,
    WithStmt,
    GotoStmt,
    LabeledStmt,
    CaseStmt,
    StmtLast      = CaseStmt,

    // -- Type expressions --
    TypeFirst,
    NamedTypeNode    = TypeFirst,
    ArrayTypeNode,
    SubrangeTypeNode,
    EnumTypeNode,
    RecordTypeNode,
    SetTypeNode,
    FileTypeNode,
    PackedTypeNode,
    PointerTypeNode,
    StringTypeNode,          // EP §6.4.3.3: string(N) variable-length string
    TypeOfNode,              // EP §6.4.9: type of x — static type inquiry
    ConformantArrayTypeNode, // EP §6.7.3.7: conformant array parameter type
    SchemaTypeNode,          // EP §6.4.8: discriminated schema instantiation
    ProcedureTypeNode,       // ISO §6.6.3.1: procedural/functional parameter
    TypeLast      = ProcedureTypeNode,

    // -- Top-level declaration / program nodes --
    ProcDeclKind,
    BlockNodeKind,
    ProgramNodeKind,
    ModuleNodeKind,   // EP §6.11: module definition (heading or body)
};

/// How many kinds each category holds.
///
/// Several places walk the tree — Sema, codegen, the AST dump, the interface
/// writer — and each does it with its own dispatch.  A switch over NodeKind
/// cannot report a kind one of them forgot: the enum spans all three
/// categories at once, so every walk needs a default for the two categories
/// that are not its own, and a default is exactly what makes a missing kind
/// quiet.  Three type denoters and a statement went unprinted for as long as
/// they had existed for that reason.
///
/// So a walk states the count it was written against instead.  Adding a kind
/// moves one of these and stops the build at every walk that named it, each
/// with a message saying what to go and teach.
inline constexpr int NumExprKinds =
    static_cast<int>(NodeKind::ExprLast) - static_cast<int>(NodeKind::ExprFirst) + 1;
inline constexpr int NumStmtKinds =
    static_cast<int>(NodeKind::StmtLast) - static_cast<int>(NodeKind::StmtFirst) + 1;
inline constexpr int NumTypeKinds =
    static_cast<int>(NodeKind::TypeLast) - static_cast<int>(NodeKind::TypeFirst) + 1;

// ---------------------------------------------------------------------------
// Base nodes
// ---------------------------------------------------------------------------

/// Base class for all AST nodes.  Carries an LLVM-style RTTI discriminator
/// and the source location of the construct.
struct Node {
    /// LLVM-style RTTI discriminator; set once at construction.
    NodeKind    Kind;
    /// Where this construct is.  Four bytes; ask a SourceManager to turn it
    /// into a filename, a line and a column.
    SourceLocation Loc;
    virtual ~Node() = default;

    /// Every concrete subclass provides classof(); the base accepts all nodes.
    static bool classof(const Node*) { return true; }

protected:
    explicit Node(NodeKind k) : Kind(k) {}
};

/// Abstract base for all expression nodes.
struct ExprNode : Node {
    static bool classof(const Node* n) {
        return n->Kind >= NodeKind::ExprFirst && n->Kind <= NodeKind::ExprLast;
    }
    /// Semantic type assigned by Sema::checkExpr(). Null before Sema runs.
    /// Mutable so Sema can annotate nodes reached through const references.
    mutable std::shared_ptr<Type> ResolvedType;

    /// R2: the ordinal value Sema folded for this expression, in the scope the
    /// expression was WRITTEN in.
    ///
    /// CodeGen has a folder of its own, and it resolves identifiers against a
    /// flat table holding whatever is innermost where the expression is being
    /// LOWERED.  Those are different questions the moment a name is redeclared
    /// between the two points, and the compiler was correct only for as long as
    /// the two answers happened to agree.  Consulting this first is what makes
    /// Sema's answer the answer; see docs/single-source-of-truth.md.
    ///
    /// Absent means Sema did not fold it -- NOT that it is zero.  Fabricating a
    /// number is how a bound that did not fold became a one-element range that
    /// every subscript then ran off the end of.
    ///
    /// Deliberately not set for a value folded against a schema's probe
    /// binding: that is the extent of no instance, and recording it would hand
    /// codegen the probe's answer for every one of them.
    mutable std::optional<int64_t> ConstVal;
protected:
    using Node::Node;
};

/// Abstract base for all statement nodes.
struct StmtNode : Node {
    static bool classof(const Node* n) {
        return n->Kind >= NodeKind::StmtFirst && n->Kind <= NodeKind::StmtLast;
    }
protected:
    using Node::Node;
};

/// Abstract base for all type-expression nodes.
struct TypeNode : Node {
    static bool classof(const Node* n) {
        return n->Kind >= NodeKind::TypeFirst && n->Kind <= NodeKind::TypeLast;
    }
    /// Semantic type assigned by Sema::resolveType(). Null before Sema runs.
    /// Codegen falls back to this for type denoters it cannot lower from the
    /// syntax alone, such as EP `type of x`.
    /// Mutable so Sema can annotate nodes reached through const references.
    mutable std::shared_ptr<Type> ResolvedType;
    /// EP §6.4.1: the denoter was written with the 'bindable' prefix, so a
    /// variable of it may be bound to an external entity.  It is a property of
    /// the declaration rather than of the type — `bindable text` and `text`
    /// describe the same values — so it is recorded here and not on Type.
    bool Bindable{false};
    /// R3: this denoter's extents as closed forms, when it is written inside a
    /// schema body.  A string's capacity and an array's or subrange's lower
    /// bound are ExtentLow; the upper bound is ExtentHigh.  Absent when the
    /// denoter is not in a schema body, or when the expression is not one
    /// buildExtentForm can close over.
    mutable std::optional<ExtentForm> ExtentLow, ExtentHigh;

    /// EP §6.6: the 'value' clause of the denoter, which says what state a
    /// variable of the type starts in.  Like Bindable it belongs to the
    /// denoter and not to the type, since `integer value 0` and `integer`
    /// describe the same values.  Null when none was written.
    std::unique_ptr<ExprNode> InitialState;
protected:
    using Node::Node;
};

} // namespace plang
