// SchemaTypeRegistry.h — EP §6.4.7 schema definitions reachable from the
// current block, so a body's bound expressions can be re-emitted with
// run-time discriminants.
//
// Pure AST: zero LLVM dependency, the most independently-testable of the
// schema-related units.
#pragma once

#include <map>
#include <string>

namespace plang {
struct BlockNode;
struct TypeNode;
struct Type;
}

class SchemaTypeRegistry {
public:
    struct SchemaDef {
        const plang::TypeNode* body{nullptr};
    };

    /// Records the schemas declared in \p block so their bodies can be
    /// re-emitted with run-time discriminants.
    void registerSchemaDefs(const plang::BlockNode& block);
    const SchemaDef* findSchemaDef(const std::string& name) const;
    /// The body denoter of a schema, from the type itself where it carries
    /// one, or (for a synthetic schema type that does not, e.g. a formal
    /// parameter's) from the name it was declared under.
    const plang::TypeNode* schemaBodyNodeOf(const plang::Type& T) const;

    /// ISO §6.2.2.3: a schema declared in a block is invisible outside it,
    /// and schemaDefs_ is flat like typeAliases/consts/requiredConsts, so
    /// emitFunctionDef snapshots and restores it around a procedure body the
    /// same way. A narrow snapshot/restore pair, not raw field access, so
    /// this stays private.
    using Snapshot = std::map<std::string, SchemaDef>;
    Snapshot snapshotDefs() const { return schemaDefs_; }
    void restoreDefs(Snapshot s) { schemaDefs_ = std::move(s); }

private:
    std::map<std::string, SchemaDef> schemaDefs_;
};
