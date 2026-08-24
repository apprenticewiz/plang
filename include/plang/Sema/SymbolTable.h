#pragma once

#include "plang/Basic/BuiltinIDs.h"

#include "plang/Basic/SourceLocation.h"
#include "plang/Sema/Type.h"

namespace plang { struct TypeNode; } // forward declaration for SchemaBodyNode
namespace plang { struct ProcDecl; } // forward declaration for Symbol::Decl
namespace plang { struct TypeDef;  } // forward declaration for Symbol::SchemaDeclTypeDef

#include <concepts>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace plang {

// ---------------------------------------------------------------------------
// SymbolKind — discriminates what a symbol represents
// ---------------------------------------------------------------------------

enum class SymbolKind {
    Var,        // ordinary variable (value parameter is also stored as Var)
    VarParam,   // 'var' (reference) parameter
    Const,      // named constant
    TypeAlias,  // user-defined type alias from a 'type' section
    Proc,       // procedure or function
    Label,      // label declared in a 'label' section
    EnumValue,  // a member of an enumerated type
    Builtin,    // built-in procedure or function (arity/type checks are relaxed)
    Schema,     // EP §6.4.7: schema type definition (parameterized type)
};

// ---------------------------------------------------------------------------
// Symbol — one entry in the symbol table
// ---------------------------------------------------------------------------

/// One entry in the symbol table.
struct Symbol {
    /// Discriminates what this symbol represents.
    SymbolKind    Kind;
    /// Lowercase canonical key (Pascal identifiers are case-insensitive).
    std::string   Name;
    /// Source location where this symbol was declared.
    SourceLocation DeclLoc;
    /// EP §6.11: the module that declares this symbol, lowercased; empty for
    /// everything declared by the program itself.  Two modules may each declare
    /// a procedure called `f`, so the module is part of what identifies it.
    std::string   Module;
    /// EP §6.11.2: the name this symbol is declared under in that module, when
    /// renaming on export or on import has made it differ from Name.  The
    /// object file knows it by this one; Name is only what the source says.
    /// Empty when nothing renamed it.
    std::string   LinkName;

    /// Declared type; used for Var, VarParam, Const, and EnumValue.
    std::shared_ptr<Type> Ty;

    /// How many scopes were open when this symbol was defined.  A schema's body
    /// is resolved standing in that many, so it binds where it was written.
    size_t ScopeDepth{0};

    /// For a TypeAlias: the denoter the type was declared with.  A `value`
    /// clause and `bindable` live on the DENOTER rather than on the Type, so
    /// asking which declaration a type name denotes is a question only the
    /// symbol table can answer correctly -- it has the scope chain that
    /// CodeGen's spelling-keyed tables do not.
    const TypeNode* TypeDeclNode{nullptr};

    // Proc / Function
    /// True if this symbol is a function (has a return value).
    bool IsFunction{false};
    /// True if this symbol was declared as a forward declaration.
    bool IsForward{false};
    /// True if this is a procedural or functional formal parameter rather than
    /// a declared procedure (ISO §6.6.3.1).  It is called the same way, but it
    /// is not a name codegen can resolve to a function — the value arrives at
    /// run time — and it cannot itself be redeclared or forward-declared.
    bool IsProcParam{false};
    /// Parameters of the procedure or function.
    std::vector<plang::Type::Param> Params;
    /// Return type; null for procedures.
    std::shared_ptr<plang::Type>    ReturnType;
    /// The heading this symbol came from, when there is one in this
    /// compilation unit.  A 'forward' declaration lends it to the defining
    /// occurrence, which under ISO §6.6.1 writes no heading of its own.
    /// Null for imported and required procedures.
    const ProcDecl* Decl{nullptr};

    // EnumValue
    /// Ordinal value of this enum constant.
    int OrdinalValue{0};

    // Const
    /// Value of a named ordinal constant, when it folded.  ISO §6.4.2.4 lets a
    /// constant-identifier stand as an array or subrange bound, so type
    /// resolution needs the value and not only the type.
    int64_t ConstOrdinal{0};
    bool    HasConstOrdinal{false};

    /// The real-valued sibling of ConstOrdinal/HasConstOrdinal, for a named
    /// constant Sema::constRealBound can fold -- narrower than the ordinal
    /// case (see ExprNode::ConstRealVal for why).
    double ConstReal{0.0};
    bool   HasConstReal{false};


    /// Set the first time the name is resolved as a value or as the target of
    /// an assignment.  What is left unset by the end of a block was declared
    /// and then never mentioned again.
    bool Referenced{false};

    // Label usage tracking
    /// Set when a LabeledStmt carries this label.
    bool LabelPlaced{false};
    /// Set when a GotoStmt names this label.
    bool LabelReferenced{false};
    /// Set when the statement this label prefixes lies inside a structured
    /// statement rather than at the outermost level of its block's statement
    /// part.  ISO §6.8.1 lets a goto in an enclosed block name a label only
    /// when it is at that outermost level, so the goto has somewhere to land
    /// that no half-finished for- or with-statement is holding open.
    bool LabelNested{false};

    /// Set by pushWithScope when this field was introduced via 'with r do' where
    /// r is a packed record — packed components cannot be passed as var params.
    bool FromPackedWith{false};

    /// EP §6.7.3.1: set for protected value parameters — assignment inside the
    /// function body is a compile-time error.
    bool IsProtected{false};

    /// EP §6.7.2: set for a named result variable, registered as an ordinary
    /// Var so it can be assigned and read by name (Sema.cpp).  Lets
    /// resultFrameFor tell "this Var IS a result variable" apart from "this
    /// Var merely shares a result-name's spelling" — a nested function's own
    /// unrelated local of the same name must shadow an ENCLOSING function's
    /// named result, not be mistaken for it.
    bool IsResultVar{false};

    /// EP §6.4.1: the declaration carried the 'bindable' prefix, so bind,
    /// unbind and binding will accept this variable.  On a TypeAlias it means
    /// every variable declared with that name is bindable.
    bool IsBindable{false};

    /// True for a required word the active dialect does not have.  Such a name
    /// is declared whatever the dialect, so that using it can say what it is
    /// rather than leaving it to look like a name the program forgot to
    /// declare.  Which dialects have which builtin is written in Builtins.def;
    /// this is that answer for the dialect in force, decided once at
    /// registration rather than re-derived at each use.
    bool NotInDialect{false};

    /// Which builtin this is, for a SymbolKind::Builtin; BuiltinID::None
    /// otherwise.  Lets a resolved call be recorded as an identity rather than
    /// re-matched on its spelling further down.
    BuiltinID BuiltinKind{BuiltinID::None};

    // --- Schema symbols (EP §6.4.7) ---
    /// One declared discriminant parameter of this schema.
    struct SchemaParam { std::string Name; std::shared_ptr<Type> Ty; };
    /// Declared discriminant parameters of the schema (in order).
    std::vector<SchemaParam> SchemaDeclParams;
    /// Pointer to the schema body TypeNode in the AST (borrowed; not owned).
    const TypeNode* SchemaBodyNode{nullptr};
    /// Pointer to the AST TypeDef this schema was declared from (borrowed;
    /// not owned; same lifetime as SchemaBodyNode above).  Set as soon as the
    /// symbol is defined, so Sema::resolveSchemaParams can resolve
    /// SchemaDeclParams and SchemaBodyNode from it lazily, on demand, rather
    /// than requiring a fixed position in the block-checking phase order.
    const TypeDef* SchemaDeclTypeDef{nullptr};
    /// Set once Sema::resolveSchemaParams has populated SchemaDeclParams (and
    /// SchemaBodyNode) for this schema, so a later call is a no-op.  A schema
    /// may be resolved on demand -- e.g. by a same-block ordinary type-alias
    /// instantiating it, or by a pointer naming it as a domain type -- before
    /// the explicit sweep over every schema in the block reaches it.
    bool SchemaParamsResolved{false};
};

// ---------------------------------------------------------------------------
// SymbolTable — scoped, case-insensitive symbol map
// ---------------------------------------------------------------------------

/// Scoped, case-insensitive symbol table.
class SymbolTable {
public:
    /// Push a new (innermost) scope onto the scope stack.
    ///
    /// \p IsBlock says whether this is a BLOCK in the sense of ISO §6.2 -- a
    /// program, procedure, function or module body.  A with-statement and a
    /// `for ... in` also open a scope, but they are not blocks, and a rule
    /// about "the block containing this statement" has to see through them.
    void pushScope(bool IsBlock = true);
    /// Pop the innermost scope from the scope stack.
    void popScope();

    /// Define a symbol in the current (innermost) scope.
    /// Returns false (without inserting) if the lowercase name already exists
    /// in the current scope. The caller emits the duplicate-declaration error.
    [[nodiscard]] bool define(Symbol Sym);

    /// Look up Name case-insensitively starting at the innermost scope.
    /// Returns nullptr if not found anywhere.
    /// ISO 10206 §6.2.2: an identifier occurrence denotes the declaration whose
    /// region encloses THAT OCCURRENCE.  A schema's body is written where the
    /// schema is declared, but it is RESOLVED once per instantiation, wherever
    /// the instantiation happens to be written -- so without this the body's
    /// names were bound in the instantiating procedure's scope.  A local
    /// `const k = 1` beside `var h: v(2)` resized a body declared against a
    /// program-scope `k = 10`, and a local `type e = char` changed what the
    /// body's elements were.
    ///
    /// Standing in the declaring scope is what the rule asks for, and this is
    /// how: lookup stops at \p Depth scopes, so the instantiation site's own
    /// declarations are not visible while the body is resolved.  The schema's
    /// discriminants are not in the symbol table at all -- they travel in
    /// ActiveSchemaBindings_ -- so they stay visible.
    struct ScopeCeiling {
        SymbolTable& T;
        size_t       Saved;
        ScopeCeiling(SymbolTable& T, size_t Depth) : T(T), Saved(T.Ceiling) {
            T.Ceiling = Depth;
        }
        ~ScopeCeiling() { T.Ceiling = Saved; }
    };
    /// How many scopes are currently open; a symbol records this when defined.
    [[nodiscard]] size_t depth() const { return Scopes.size(); }

    [[nodiscard]] Symbol*       lookup(const std::string& Name);
    [[nodiscard]] const Symbol* lookup(const std::string& Name) const;

    /// Look up only in the innermost scope.
    [[nodiscard]] Symbol*       lookupCurrent(const std::string& Name);
    [[nodiscard]] const Symbol* lookupCurrent(const std::string& Name) const;

    /// Look up from the innermost scope out to and including the nearest
    /// BLOCK, and no further.
    ///
    /// ISO §6.8.3.9 asks whether a for-statement's control variable is
    /// declared in the block containing it.  lookupCurrent stood in for that,
    /// and a with-statement opens a scope of its own: inside one, the
    /// innermost scope holds the record's fields and nothing else, so a
    /// conforming `with r do for i := 1 to 3 do ...` was rejected with "must
    /// be declared in the immediately enclosing block" about a variable that
    /// was.
    [[nodiscard]] const Symbol* lookupInEnclosingBlock(const std::string& Name) const;

    /// Iterate every symbol in the current scope (used for post-body label audits).
    template<std::invocable<Symbol&> Fn>
    void forEachInCurrentScope(Fn&& F) {
        if (!Scopes.empty()) {
            for (auto& [K, Sym] : Scopes.back().Symbols) F(Sym);
        }
    }

private:
    static std::string lower(const std::string& S);

    struct Scope {
        std::unordered_map<std::string, Symbol> Symbols; // key is lowercase
        bool IsBlock{true};                              // see pushScope
    };
    std::vector<Scope> Scopes;
    /// How many scopes lookup may see, or 0 for all of them.  See ScopeCeiling.
    size_t Ceiling{0};
};

} // namespace plang
