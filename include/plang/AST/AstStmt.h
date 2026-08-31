#pragma once

#include "plang/AST/AstExpr.h"  // for ExprNode (statements contain expression children)

namespace plang {

// ---------------------------------------------------------------------------
// Statement nodes
// ---------------------------------------------------------------------------

struct AssignStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::AssignStmt; }
    AssignStmt() : StmtNode(NodeKind::AssignStmt) {}
    std::unique_ptr<ExprNode> Target;  /// LValue: IdentExpr, IndexExpr, FieldExpr, DerefExpr
    std::unique_ptr<ExprNode> Value;   /// RHS expression
};

struct CompoundStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::CompoundStmt; }
    CompoundStmt() : StmtNode(NodeKind::CompoundStmt) {}
    std::vector<std::unique_ptr<StmtNode>> Stmts;  /// statements between begin and end
};

struct IfStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::IfStmt; }
    IfStmt() : StmtNode(NodeKind::IfStmt) {}
    std::unique_ptr<ExprNode> Cond;  /// boolean condition
    std::unique_ptr<StmtNode> Then;  /// then branch
    std::unique_ptr<StmtNode> Else;  /// else branch; null if absent
};

struct WhileStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::WhileStmt; }
    WhileStmt() : StmtNode(NodeKind::WhileStmt) {}
    std::unique_ptr<ExprNode> Cond;  /// condition tested before each iteration
    std::unique_ptr<StmtNode> Body;
};

struct ForStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ForStmt; }
    ForStmt() : StmtNode(NodeKind::ForStmt) {}
    std::string               Var;            /// loop control variable name
    std::unique_ptr<ExprNode> From, Limit;    /// initial and terminal values
    bool                      Downto{false};  /// false = to, true = downto
    std::unique_ptr<StmtNode> Body;

    /// The control variable's own declared type, set by Sema::checkFor
    /// (Sym->Ty) once it has resolved Var to a Var/VarParam symbol; null
    /// where that lookup failed and a diagnostic already fired.  Var is a
    /// bare name, not an ExprNode, so unlike From/Limit it has no
    /// ResolvedType of its own for CodeGen to read -- this is that answer,
    /// following the same "Sema attaches what CodeGen needs onto the
    /// statement" precedent as CallStmt::ResolvedType.
    ///
    /// CGControlFlow::emitFor needs it for a reason From/Limit's OWN types
    /// cannot always supply: both bounds are coerced into the control
    /// variable's storage before the loop ever compares them, so what the
    /// comparison must respect is the STORAGE's signedness, not either
    /// bound's pre-coercion one.  Most of the time those agree -- a bound
    /// that is itself a variable/constant of a compatible type carries the
    /// same signedness the control variable does -- but a bare integer
    /// literal bound (`for w := 0 to 65535 do`) is always typed as the
    /// dialect's plain signed Integer regardless of value, so reading only
    /// From/Limit's types read Word's own 0..65535 loop as fully signed and
    /// silently ran zero iterations.
    mutable std::shared_ptr<Type> VarType;
};

struct RepeatStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::RepeatStmt; }
    RepeatStmt() : StmtNode(NodeKind::RepeatStmt) {}
    std::vector<std::unique_ptr<StmtNode>> Stmts;  /// body before each test
    std::unique_ptr<ExprNode>              Cond;   /// exits when true
};

struct CallStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::CallStmt; }
    CallStmt() : StmtNode(NodeKind::CallStmt) {}
    std::string                            Name;  /// procedure name
    std::vector<std::unique_ptr<ExprNode>> Args;

    /// Which required procedure Sema resolved this call to, or None where it
    /// resolved to one the program declared.  See CallExpr::ResolvedBuiltin.
    mutable BuiltinID ResolvedBuiltin{BuiltinID::None};

    /// Turbo `{$X+}`'s "a function may be called as a statement, its result
    /// discarded" (ISO §6.8.2.2 refuses this outright, so outside {$X+} this
    /// stays null): the callee's Sema-resolved return type, set only when
    /// Sema::checkUserDefinedCall/checkCallStmt actually let a function
    /// through this way.  Without it, CGProcCall::emitUserProcCall's
    /// not-yet-defined-in-this-module fallback had no way to know the
    /// callee's real return type and manufactured a void FunctionType,
    /// which conflicted with the function's real, non-void declaration the
    /// moment the SAME external function was also called anywhere as an
    /// expression in this translation unit.  Null for an ordinary procedure
    /// call, exactly like ExprNode::ResolvedType is null before Sema runs.
    mutable std::shared_ptr<Type> ResolvedType;

    /// Turbo Tier 5, Cluster A item 6: set by Sema::checkNewInit only when
    /// Name is "new" and this is 'new(p, Ctor(args))' for a pointer to an
    /// object type -- the constructor's name as written (its OWN chain
    /// position, i.e. which ancestor actually implements it, is
    /// re-derived by CodeGen the same way an ordinary method call's own
    /// CodeGen does, rather than stored twice).  Empty otherwise.  Args[1]
    /// itself (a CallExpr) still carries the constructor's own argument
    /// list -- CGProcCall re-reads it from there rather than a duplicate
    /// copy.
    mutable std::string NewInitMethod;
    /// Turbo Tier 5, Cluster A item 6: NewInitMethod's mirror for
    /// 'dispose(p, Dtor[(args)])', set by Sema::checkDisposeDone.
    mutable std::string DisposeDoneMethod;
};

/// Turbo Tier 5, Cluster A item 3: a method call used as a STATEMENT --
/// `Obj.Method(args);`, `P^.Method(args);`, or the bare-call form with no
/// parens at all, `Obj.Method;` (confirmed legal against a local fpc -Mtp
/// build, matching how a bare `Foo;` calls a zero-argument ordinary
/// procedure).  See MethodCallExpr (AstExpr.h) for the expression-context
/// sibling and for why the parser builds this shape unconditionally,
/// leaving Sema (Sema::checkMethodCallStmt) to confirm it is really a
/// method call at all.
///
/// The bare-call form is built by Parser::parseStatement from a FieldExpr
/// Lval with an empty Args list -- there is no separate parenthesized-args
/// production to fall back to, exactly like CallStmt's own bare-identifier
/// form just above.
struct MethodCallStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::MethodCallStmt; }
    MethodCallStmt() : StmtNode(NodeKind::MethodCallStmt) {}
    std::unique_ptr<ExprNode>              Receiver;  /// object-typed expression (or P^ deref of one)
    std::string                            Method;    /// method name, as written
    std::vector<std::unique_ptr<ExprNode>> Args;       /// actual arguments, in order; empty for the bare form

    /// Turbo `{$X+}`: mirrors CallStmt::ResolvedType -- the callee's
    /// Sema-resolved return type when a function method was let through as a
    /// statement with its result discarded, null for a procedure method or
    /// any error.  Not consumed by CodeGen yet (no method-call CodeGen
    /// exists at all -- see the ICE in CGExprCore.cpp), but recorded now so
    /// that later item does not have to re-derive it.
    mutable std::shared_ptr<Type> ResolvedType;
};

/// Turbo Tier 5, Cluster A item 5: 'inherited [Method[(args)]];' inside an
/// object method's own body -- a STATIC call (never through the VMT, even
/// when the method it reaches is itself declared 'virtual') to the DIRECT
/// PARENT's own implementation of a method with the given name.  Confirmed
/// against a local `fpc -Mtp` build: 'inherited' never redispatches, and the
/// search for which ancestor's own body actually implements the name starts
/// at the enclosing method's OwnerType's immediate Parent (never at OwnerType
/// itself, so a method can never "inherited" its own body) and walks upward
/// exactly like an ordinary method call's own ancestor-chain resolution
/// (Sema::checkMethodCall) -- see Sema::checkInheritedCallStmt.
///
/// Two surface forms, both real Borland/FPC syntax:
///   'inherited MethodName(args);' / 'inherited MethodName;' -- Method is
///   the name as written, Args as written (possibly empty).
///   'inherited;' (bare) -- Method is empty; means "the same method THIS
///   method body itself overrides, called with the same arguments this
///   activation itself received" -- CodeGen forwards this activation's own
///   parameters unchanged rather than re-marshalling anything (see
///   CGProcCall::emitInheritedCallStmt), which is sound only because
///   Sema's own override-signature check (resolveObjectType,
///   sameMethodSignature) already guarantees the ancestor's own parameter
///   list is identical.
struct InheritedCallStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::InheritedCallStmt; }
    InheritedCallStmt() : StmtNode(NodeKind::InheritedCallStmt) {}
    /// The method name as written, or empty for the bare 'inherited;' form.
    std::string                            Method;
    /// Actual arguments, in order; always empty for the bare form (see
    /// Method's own comment -- CodeGen supplies this activation's own
    /// parameters directly, with no Args to marshal).
    std::vector<std::unique_ptr<ExprNode>> Args;

    /// Sema::checkInheritedCallStmt's resolution, consumed by
    /// CGProcCall::emitInheritedCallStmt:
    /// The method name actually being resolved -- Method verbatim for the
    /// explicit form, or the enclosing method's own name for the bare form.
    mutable std::string ResolvedMethod;
    /// The ancestor object type (in CurrentProc's OwnerType's own Parent
    /// chain) whose OWN implementation this statically calls -- mirrors
    /// MethodCallExpr/Stmt codegen's identical "Owner" concept, just found
    /// starting one level higher (Parent, not the receiver's own type).
    mutable std::string ImplementingType;
    /// Turbo `{$X+}`: mirrors MethodCallStmt::ResolvedType exactly, for the
    /// explicit-name form only (the bare form's own callee is a procedure or
    /// function according to whatever the ENCLOSING method already is, and
    /// its result -- if any -- is unused here regardless).
    mutable std::shared_ptr<Type> ResolvedType;
};

struct WithStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::WithStmt; }
    WithStmt() : StmtNode(NodeKind::WithStmt) {}
    std::vector<std::unique_ptr<ExprNode>> Records;  /// record variables opened in scope
    std::unique_ptr<StmtNode>              Body;
};

struct GotoStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::GotoStmt; }
    GotoStmt() : StmtNode(NodeKind::GotoStmt) {}
    std::string Label;  /// target label (integer string or identifier)
};

struct LabeledStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::LabeledStmt; }
    LabeledStmt() : StmtNode(NodeKind::LabeledStmt) {}
    std::string               Label;
    std::unique_ptr<StmtNode> Stmt;  /// null for an empty label
};

/// A single case label: a point value or a lo..hi range (EP §6.9.3.5).
struct CaseLabel {
    std::unique_ptr<ExprNode> Low;
    std::unique_ptr<ExprNode> High; /// null for point labels; non-null for lo..hi ranges
};

/// One arm of a case statement: a list of constant labels and a body statement.
struct CaseArm {
    std::vector<CaseLabel>    Labels;  /// case-constant expressions / ranges
    std::unique_ptr<StmtNode> Body;
};

/// EP §6.9.3.9.3: for v in set-expr do stmt — set-member iteration.
/// The compiler introduces an implicit loop variable of the set's element type.
struct ForInStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ForInStmt; }
    ForInStmt() : StmtNode(NodeKind::ForInStmt) {}
    std::string               Var;     /// loop variable name (declared implicitly)
    std::unique_ptr<ExprNode> SetExpr; /// set expression to iterate
    std::unique_ptr<StmtNode> Body;
};

/// case index of const-list : stmt ; ... end  — ISO §6.8.3.5
struct CaseStmt : StmtNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::CaseStmt; }
    CaseStmt() : StmtNode(NodeKind::CaseStmt) {}
    std::unique_ptr<ExprNode>  Selector;  /// the case-index expression (must be ordinal)
    std::vector<CaseArm>       Arms;      /// case arms in source order
    std::unique_ptr<StmtNode>  Else;      /// optional else branch (common extension; null in strict ISO)
    /// Whether an 'otherwise' or 'else' part was written.  Distinct from a
    /// non-null Else, because EP §6.9.3.5 makes the part a statement and the
    /// empty statement is one: `case i of 1: f otherwise end` is the idiom for
    /// ignoring everything else, and must not be an unmatched-value error.
    bool HasElse{false};
};

} // namespace plang
