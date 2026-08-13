#pragma once

#include <set>
#include "plang/AST/AstExpr.h"   // for ExprNode (ConstDef::Value)
#include "plang/AST/AstType.h"   // for TypeNode (ParamGroup::Type, ProcDecl::ReturnType)
#include "plang/AST/AstStmt.h"   // for CompoundStmt (BlockNode::Body)

namespace plang {

// ---------------------------------------------------------------------------
// Declaration and program nodes
// ---------------------------------------------------------------------------

struct ConstDef {
    std::string               Name;   /// constant name
    std::unique_ptr<ExprNode> Value;  /// value expression
};

/// One group of discriminant parameters sharing a type (EP §6.4.7).
/// Example: "m, n : integer" → SchemaParamSpec{ Names={"m","n"}, TypeName="integer" }
struct SchemaParamSpec {
    std::vector<std::string> Names;    ///< discriminant names in this group
    std::string              TypeName; ///< ordinal type name (e.g. "integer")
};

struct TypeDef {
    std::string                  Name;
    /// Non-empty iff this is a schema definition (EP §6.4.7).
    std::vector<SchemaParamSpec> SchemaParams;
    /// EP §6.4.1: 'bindable' prefix (no codegen effect in Tier 7).
    bool                         IsBindable{false};
    std::unique_ptr<TypeNode>    Type;
};

struct VarGroup {
    std::vector<std::string>  Names;
    std::unique_ptr<TypeNode> Type;
    /// EP §6.4.1: optional 'value expr' initializer; null if absent.
    std::unique_ptr<ExprNode> InitExpr;
};

// ParamGroup lives in AstType.h: a procedural parameter's type is written as a
// parameter list of its own, so the type nodes need it too.

struct BlockNode;   // forward decl: ProcDecl and BlockNode are mutually referential

struct ProcDecl : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ProcDeclKind; }
    /// Defined out of line below: both special members instantiate
    /// ~unique_ptr<BlockNode>, which needs BlockNode to be complete.
    ProcDecl();
    ~ProcDecl();
    bool IsFunction{false};
    bool IsForward{false};             /// true if this is only a forward declaration (no body)
    std::string                Name;
    std::string                ResultName; /// EP §6.7.2: optional named result variable (empty if not specified)
    std::vector<ParamGroup>    Params;
    std::unique_ptr<TypeNode>  ReturnType;  /// null for procedures
    std::unique_ptr<BlockNode> Body;        /// null for forward declarations

    /// Which value parameters the body modifies, lower case.
    ///
    /// ISO §6.6.3.3 makes a value parameter a variable of its own, so a
    /// conformant array passed by value has to be copied -- and the copy is as
    /// big as the actual, which is only known when the call arrives.  A body
    /// that never modifies the formal cannot tell the difference, so it gets no
    /// copy at all: that is what keeps a large array passed down a recursion
    /// from exhausting the stack, which an unconditional copy did.
    ///
    /// Sema fills this in because deciding it needs the callee's signature at
    /// every call: passing the formal on as somebody else's `var` parameter
    /// modifies it, and passing it as a value parameter does not.
    mutable std::set<std::string> ModifiedParams;

    /// ISO §6.6.1: the defining occurrence of a procedure declared 'forward'
    /// writes the heading again as the name alone — neither the parameter list
    /// nor the result type is repeated, since the declaration already gave
    /// them.  Sema points this at that declaration; null when the heading
    /// standing here is complete.
    mutable const ProcDecl* ForwardHeading{nullptr};

    /// Where the parameters and result type are to be read from.
    [[nodiscard]] const ProcDecl& heading() const {
        return ForwardHeading ? *ForwardHeading : *this;
    }
};

struct BlockNode : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::BlockNodeKind; }
    BlockNode() : Node(NodeKind::BlockNodeKind) {}
    std::vector<std::string>               Labels;
    std::vector<ConstDef>                  Consts;
    std::vector<TypeDef>                   Types;
    std::vector<VarGroup>                  Vars;
    std::vector<std::unique_ptr<ProcDecl>> Procs;
    std::unique_ptr<CompoundStmt>          Body;
};

inline ProcDecl::ProcDecl()  : Node(NodeKind::ProcDeclKind) {}
inline ProcDecl::~ProcDecl() = default;

// ---------------------------------------------------------------------------
// EP §6.11: Module support
// ---------------------------------------------------------------------------

/// One export-clause of an export-list.  EP §6.11.2.
struct ExportItem {
    SourceLocation Loc;           ///< the name being exported
    std::string Name;             ///< identifier as the module declares it
    std::string Alias;            ///< name after '=>'; empty if not renamed
    /// Last name of an export-range, `first..last`; empty for a single name.
    /// The range covers the constants of one enumerated type, which is the
    /// only run of names the standard gives a first and a last.
    std::string RangeEnd;
    bool        Protected{false}; ///< importers may read it but not assign to it
};

/// One import clause.  EP §6.11.3.
struct ImportClause {
    SourceLocation           Loc;              ///< the 'import' keyword
    std::string              ModuleName;       ///< module to import from
    bool                     Qualified{false}; ///< import M qualified; — access as M.name
    /// True when 'only' was written, making Names the whole of what is
    /// imported.  Without it a name list still renames, but everything the
    /// interface exports comes in.
    bool                     Selective{false};
    /// The import-list as written, before renaming.  Empty means no list.
    std::vector<std::string> Names;
    /// Renames: 'import M (a => b)' → {{"a","b"}}
    std::vector<std::pair<std::string,std::string>> Renames;
};

struct ModuleNode;  // forward decl

struct ProgramNode : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ProgramNodeKind; }
    // Defined below, once ModuleNode is complete: destroying OwnedModules
    // needs its size, which an implicit destructor here would not have.
    ProgramNode();
    ~ProgramNode();
    std::string                Name;
    std::vector<std::string>   FileParams;  ///< program heading file-parameter list (e.g. input, output)
    std::vector<ImportClause>  Imports;     ///< import clauses in the program heading (EP §6.11.3)
    std::vector<std::unique_ptr<ModuleNode>> OwnedModules; ///< module definitions before this program
    std::vector<ModuleNode*>   Modules;     ///< borrowed ptrs into OwnedModules
    std::unique_ptr<BlockNode> Block;
};

/// EP §6.11: module definition (heading or body).
struct ModuleNode : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ModuleNodeKind; }
    ModuleNode() : Node(NodeKind::ModuleNodeKind) {}
    std::string                Name;
    std::vector<std::string>   Params;         ///< module parameters (input, output, ...)
    bool                       IsInterface{false}; ///< true = heading (interface)
    /// EP §6.11.1: written 'implementation', which gives the module block the
    /// declarations of the interface of the same name.
    bool                       IsImplementation{false};
    std::vector<ExportItem>    Exports;        ///< exported names (heading only)
    std::vector<ImportClause>  Imports;        ///< imported modules (body/program)
    std::unique_ptr<BlockNode> Body;           ///< declarations and procedures
    std::unique_ptr<StmtNode>  InitStmt;       ///< to begin do stmt (body only)
    std::unique_ptr<StmtNode>  FinalStmt;      ///< to end do stmt (body only)
};

inline ProgramNode::ProgramNode()  : Node(NodeKind::ProgramNodeKind) {}
inline ProgramNode::~ProgramNode() = default;

} // namespace plang
