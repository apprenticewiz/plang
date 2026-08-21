#include "SchemaTypeRegistry.h"

#include "plang/AST/Ast.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Sema/Type.h"

using namespace plang;

void SchemaTypeRegistry::registerSchemaDefs(const BlockNode& block) {
    for (const auto& td : block.Types) {
        if (td.SchemaParams.empty() || !td.Type) continue;
        SchemaDef def;
        def.body = td.Type.get();
        schemaDefs_[toLower(td.Name)] = std::move(def);
    }
}

const SchemaTypeRegistry::SchemaDef*
SchemaTypeRegistry::findSchemaDef(const std::string& name) const {
    auto it = schemaDefs_.find(toLower(name));
    return it != schemaDefs_.end() ? &it->second : nullptr;
}

const TypeNode* SchemaTypeRegistry::schemaBodyNodeOf(const Type& T) const {
    if (T.SchemaBodyNode) return T.SchemaBodyNode;
    const SchemaDef* def = findSchemaDef(T.SchemaName);
    return def ? def->body : nullptr;
}
