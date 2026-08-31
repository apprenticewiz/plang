#include "OrdinalSignedness.h"

#include "plang/AST/Ast.h"
#include "plang/Sema/Type.h"

using namespace plang;

bool ordinalIsUnsigned(const Type* t) {
    return t && !t->IsSigned;
}

bool exprIsSigned(const ExprNode& e) {
    return !ordinalIsUnsigned(e.ResolvedType.get());
}
