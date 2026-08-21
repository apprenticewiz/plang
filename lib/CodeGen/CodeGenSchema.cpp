// CodeGenSchema.cpp — EP §6.4.7 schema type/body registry forwarders.
//
// Schema value/access-path resolution (schemaRefOf, schemaPathOf,
// emitNewSchema, and the rest) moved to SchemaAccess; the EP §6.4.7
// run-time layout walk moved to SchemaLayoutEngine before that. What's
// left here is the schema *declaration* registry -- registerSchemaDefs/
// findSchemaDef/schemaBodyNodeOf are already one-line forwarders straight
// to SchemaTypeRegistry, so they stay put rather than gaining a second,
// pointless hop through SchemaAccess.

#include "CodeGenImpl.h"

void Codegen::Impl::registerSchemaDefs(const BlockNode& block) {
    schemaTypes_->registerSchemaDefs(block);
}

const SchemaTypeRegistry::SchemaDef*
Codegen::Impl::findSchemaDef(const std::string& name) const {
    return schemaTypes_->findSchemaDef(name);
}

const TypeNode* Codegen::Impl::schemaBodyNodeOf(const plang::Type& T) const {
    return schemaTypes_->schemaBodyNodeOf(T);
}
