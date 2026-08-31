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
    /// Turbo's "typed constant" form: `identifier ':' type-expr '=' value`.
    /// Null for a plain, untyped ISO/EP `const` (the common case, every
    /// dialect).  When set, this is NOT a real constant at all -- TP7 gives
    /// it static storage and a one-time initializer, and it may be assigned
    /// to like a variable; Sema reflects that by registering it as a
    /// SymbolKind::Var (see Symbol::IsTypedConst) rather than
    /// SymbolKind::Const.
    std::unique_ptr<TypeNode>  Type;
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
    /// Where each name in Names was written, index-aligned with Names.  Used
    /// so diagnostics about one name in a multi-name group ("a, b: integer")
    /// point at that name's own token rather than at the shared type.
    std::vector<SourceLocation> NameLocs;
    std::unique_ptr<TypeNode> Type;
    /// EP §6.4.1: optional 'value expr' initializer; null if absent.
    std::unique_ptr<ExprNode> InitExpr;
    /// Turbo's 'absolute' directive: `var W: Word absolute B;` overlays this
    /// declaration's storage directly onto the variable (or component, e.g.
    /// `absolute B[0]`) this expression names, instead of allocating storage
    /// of its own.  Null when no 'absolute' clause was written.  Only ever
    /// set for a single-name group -- real Turbo Pascal writes 'absolute' on
    /// one variable at a time, and Sema rejects it on a multi-name group.
    std::unique_ptr<ExprNode> AbsoluteExpr;
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

    // -- Turbo Tier 5, Cluster A item 0: object-type method attributes --
    // Parsing only: see ObjectTypeNode's own comment for the whole design.
    // Flat bools, following this struct's existing IsFunction/IsForward idiom
    // rather than a new abstraction.
    bool IsVirtual{false};      /// trailing '; virtual;' directive on the heading
    bool IsAbstract{false};     /// trailing '; abstract;' directive (always with IsVirtual)
    bool IsConstructor{false};  /// 'constructor' rather than 'procedure'
    bool IsDestructor{false};   /// 'destructor' rather than 'procedure'/'function'
    /// The object type this is an OUT-OF-LINE method body for, e.g. the
    /// 'TAnimal' in 'procedure TAnimal.Speak; begin ... end;' -- confirmed
    /// against a local fpc -Mtp build that this dotted heading is real Turbo
    /// Pascal syntax and that the qualifier is a plain type name, not
    /// re-resolved until Sema runs (see ImportClause::ModuleName's own
    /// comment for the same "name a not-yet-resolved cross-reference as a
    /// plain string" precedent).  Left empty for an IN-CLASS method heading
    /// (one that is itself a member of an ObjectTypeNode::Members list) --
    /// that association is already structural, the ProcDecl being reached
    /// only by walking that very list, so recording the same name a second
    /// time here would just be a second thing that could go out of sync with
    /// the first.  Also empty for an ordinary (non-method) procedure/function.
    std::string OwnerType;

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
// Turbo Tier 5, Cluster A item 0: object types (parsing only)
// ---------------------------------------------------------------------------

/// Visibility of one ObjectMember.  Confirmed against a local fpc -Mtp build
/// (Borland TP 7.0+) that real Turbo Pascal objects use SECTION-based
/// visibility -- 'private'/'public' each open a run of members that keeps
/// that visibility until the next such keyword or 'end' -- and that, unlike
/// a Delphi class, a TP7 object allows any number of 'private'/'public'
/// sections in any order (private-then-public-then-private-again compiled
/// cleanly).  So this is stamped onto each member individually at parse
/// time (one enum value per ObjectMember) rather than modeled as a smaller
/// number of section objects -- Sema and any later consumer then just reads
/// a member's own Vis and never has to re-derive "which section is this
/// member in".
enum class MemberVisibility { Public, Private };

/// One member of an object-type's member list: EITHER a field (reusing
/// FieldDecl, exactly like RecordTypeNode::Fields) OR a method heading
/// (reusing ProcDecl, with IsForward left false and Body left null --
/// mirroring how a unit interface's own HeadingsOnly ProcDecl already means
/// "signature now, body elsewhere", ParseUnit.cpp/ParseModule.cpp's
/// parseProcDecl(..., HeadingOnly=true) path).  A single tagged struct
/// rather than two parallel vectors: real Turbo Pascal interleaves fields
/// and methods in one declaration list, and the grammar comment on
/// ObjectTypeNode below needs that same order preserved for anything that
/// walks it (Sema's future layout pass, in particular, since a field's
/// offset depends on what was declared before it).
struct ObjectMember {
    MemberVisibility           Vis{MemberVisibility::Public};
    bool                       IsMethod{false};
    FieldDecl                  Field;   /// valid when !IsMethod
    std::unique_ptr<ProcDecl>  Method;  /// valid when IsMethod
};

/// Turbo object-type denoter (parsing only -- see the module-level comment
/// on this whole item for what is deliberately NOT here yet: no ancestor
/// resolution, no VMT/layout, no method-body type-checking).
///
///   object-type → 'object' [ '(' ancestor-type-name ')' ] object-member-list 'end'
///   object-member-list → { field-declaration | method-heading | visibility-section }
///
/// Confirmed against a local fpc -Mtp build:
///   - The ancestor clause is optional; a "root" object type (TAnimal =
///     object ... end, no ancestor at all) is legal and simply starts its
///     own layout from scratch.
///   - 'virtual'/'abstract' are TRAILING directives after the heading's own
///     ';', exactly like this codebase's existing 'forward' -- e.g.
///     'procedure Draw; virtual; abstract;' -- never written before
///     'procedure'/'function'/'constructor'/'destructor'.
///   - An out-of-line method BODY ('procedure TAnimal.Speak; begin ... end;')
///     repeats the full heading (params, return type) but NEVER repeats
///     'virtual'/'abstract' -- fpc rejects "VIRTUAL not allowed in
///     implementation section" -- so those two directives are parsed only
///     for an in-class ObjectMember::Method heading, never for the
///     dotted out-of-line ProcDecl (see ProcDecl::OwnerType's own comment
///     for how that dotted form is represented instead).
struct ObjectTypeNode : TypeNode {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ObjectTypeNode; }
    ObjectTypeNode() : TypeNode(NodeKind::ObjectTypeNode) {}
    /// The ancestor object type's own name, e.g. the 'TAnimal' in
    /// 'TDog = object(TAnimal) ... end'.  Empty for a root object type with
    /// no ancestor.  A plain string, resolved later by Sema -- the same
    /// not-yet-resolved-cross-reference precedent as ImportClause::ModuleName.
    std::string                Ancestor;
    /// Fields and method headings, in declaration order, each carrying its
    /// own visibility.  See ObjectMember's own comment for why one ordered,
    /// tagged list rather than separate Fields/Methods vectors.
    std::vector<ObjectMember>  Members;
};

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
struct UnitNode;    // forward decl (Turbo Tier 4, defined below)

/// One unit named in a Turbo `uses` clause: just a name and where it was
/// written.  Turbo's `uses` has none of EP's ImportClause syntax at all --
/// no `qualified`, no selective/only import list, no renaming -- so a plain
/// name+location pair is the whole of it; no existing AST type already
/// paired the two so this is new, not reused.  Moved up here (out of the
/// "Turbo Tier 4: unit support" section below, where UnitNode's own
/// InterfaceUses/ImplementationUses first needed it) so that ProgramNode's
/// own Uses field, just below, can name a complete type rather than an
/// incomplete one.
struct UsedUnit {
    SourceLocation Loc;   ///< where the unit name was written in the uses clause
    std::string    Name;  ///< unit name as written
};

struct ProgramNode : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::ProgramNodeKind; }
    // Defined below, once ModuleNode/UnitNode are complete: destroying
    // OwnedModules/BareUnit needs their size, which an implicit destructor
    // here would not have.
    ProgramNode();
    ~ProgramNode();
    std::string                Name;
    std::vector<std::string>   FileParams;  ///< program heading file-parameter list (e.g. input, output)
    std::vector<ImportClause>  Imports;     ///< import clauses in the program heading (EP §6.11.3)
    /// Turbo Tier 4: this program's own top-level `uses` clause, empty if
    /// none was written.  Turbo-only -- ISO 7185/Extended Pascal programs use
    /// Imports above instead.  See UnitNode's own InterfaceUses/
    /// ImplementationUses for why a unit keeps two separate lists of these;
    /// a program has only ever the one.
    std::vector<UsedUnit>      Uses;
    std::vector<std::unique_ptr<ModuleNode>> OwnedModules; ///< module definitions before this program
    std::vector<ModuleNode*>   Modules;     ///< borrowed ptrs into OwnedModules
    std::unique_ptr<BlockNode> Block;

    /// Turbo Tier 4: set when this whole file was a standalone
    /// `unit Name; interface ... implementation ... end.` -- no `program`
    /// heading anywhere in it, nothing here executable on its own.  Null for
    /// every ordinary program, including EP's own module-only files (those
    /// still synthesize a real, if empty, executable ProgramNode --
    /// parseMultiUnitFile's own comment).
    ///
    /// Parser::parse() keeps returning unique_ptr<ProgramNode> rather than
    /// growing a second top-level return type (a tagged union/variant is not
    /// an idiom this codebase uses anywhere else) because every caller of
    /// parse() -- today just Frontend.cpp's one call site -- already has to
    /// branch on "was this actually a program" the same way it would have to
    /// branch on a variant's active alternative, and ProgramNode already had
    /// the "one node embeds a different kind of node" idiom in OwnedModules/
    /// Modules above.  Name/Block above are harmless placeholders in this
    /// case (an empty block, the unit's own name) so that nothing downstream
    /// has to null-check them just because a unit file also went through
    /// this constructor; Frontend.cpp checks BareUnit before ever handing
    /// the ProgramNode to Sema, so those placeholders are never observed.
    std::unique_ptr<UnitNode>  BareUnit;
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

// ---------------------------------------------------------------------------
// Turbo Tier 4: unit support (interface/implementation, no ISO 10206 analog)
// ---------------------------------------------------------------------------

/// Turbo `unit`: a separately-compiled unit (Tier 4, Cluster A).
///
/// Structurally similar to EP's ModuleNode above -- both split into an
/// interface (heading) section and an implementation (body) section -- but
/// deliberately NOT the same node, because the two are genuinely different
/// shapes, not just differently spelled:
///   - no module parameters at all (contrast ModuleNode::Params, e.g. the
///     `(input, output)` a module may declare -- Turbo units have nothing
///     like it)
///   - the interface section exports everything it declares, unconditionally
///     -- Turbo's `interface` has no export-list syntax at all (contrast
///     ExportItem's rename/protected/export-range forms), so there is
///     nothing here shaped like ModuleNode::Exports
///   - at most one initialization block and no separate finalization keyword
///     -- a unit's optional `begin ... end` is InitBody below (contrast
///     ModuleNode::InitStmt/FinalStmt's separate `to begin do`/`to end do`)
///   - `uses` appears (each optional) in BOTH the interface and the
///     implementation section, as two genuinely separate scopes -- Tier 4's
///     own goal of closing mutual dependence through the implementation
///     uses clause needs both kept apart, so there is one InterfaceUses and
///     one ImplementationUses rather than a single shared list
struct UnitNode : Node {
    static bool classof(const Node* n) { return n->Kind == NodeKind::UnitNodeKind; }
    UnitNode() : Node(NodeKind::UnitNodeKind) {}
    std::string             Name;

    /// `uses` clause inside `interface`, empty if none was written.
    std::vector<UsedUnit>   InterfaceUses;
    /// const/type/var declarations and procedure/function HEADINGS (no
    /// bodies) declared in the interface section.  Each ProcDecl in here has
    /// IsForward set the same way EP's own HeadingsOnly module interface
    /// does (ParseDecl.cpp's parseProcDecl) -- a signature with the body
    /// given elsewhere, which is exactly what a heading-only declaration is.
    std::unique_ptr<BlockNode> InterfaceBlock;

    /// `uses` clause inside `implementation`, empty if none was written.
    /// Genuinely separate scope from InterfaceUses: a unit may name a unit
    /// here that itself names this unit back in ITS OWN implementation uses
    /// clause (mutual implementation-uses) -- Cluster A item 2's job to
    /// make semantically work, not this item's, but the parser accepts the
    /// syntax shape either way since parsing has no forward-reference
    /// problem the way name resolution does.
    std::vector<UsedUnit>   ImplementationUses;
    /// const/type/var declarations, full bodies for every interface-declared
    /// procedure/function, and any additional implementation-private
    /// declarations.
    std::unique_ptr<BlockNode> ImplementationBlock;

    /// The unit's single optional `begin ... end` initialization section.
    /// Null when the implementation section runs straight into `end.` with
    /// no `begin` at all -- confirmed against real `fpc -Mtp` (see this
    /// item's own report): a unit with no initialization code omits the
    /// whole block, there is no empty `begin end` requirement.
    std::unique_ptr<CompoundStmt> InitBody;
};

inline ProgramNode::ProgramNode()  : Node(NodeKind::ProgramNodeKind) {}
inline ProgramNode::~ProgramNode() = default;

} // namespace plang
