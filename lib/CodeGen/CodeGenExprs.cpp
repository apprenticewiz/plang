#include "CodeGenImpl.h"
using namespace plang;

// See NumExprKinds in AstBase.h.
static_assert(NumExprKinds == 16, "a new expression needs a case in emitExpr");

// ====================================================================
// Type coercion helpers
// ====================================================================

llvm::Value* Codegen::Impl::ensureI1(llvm::Value* v) {
    if (!v) codegenICE("boolean conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(1)) return v;
    // Truncate any integer to i1.
    return builder.CreateTrunc(v, i1Ty, "to.i1");
}

llvm::Value* Codegen::Impl::toDouble(llvm::Value* v) {
    if (!v) codegenICE("real conversion of an unlowerable expression");
    if (v->getType()->isDoubleTy()) return v;
    return builder.CreateSIToFP(v, dblTy, "to.dbl");
}

llvm::Value* Codegen::Impl::toI64(llvm::Value* v) {
    if (!v) codegenICE("integer conversion of an unlowerable expression");
    if (v->getType()->isIntegerTy(64)) return v;
    if (v->getType()->isDoubleTy())
        return builder.CreateFPToSI(v, i64Ty, "to.i64");
    return builder.CreateZExt(v, i64Ty, "to.i64");
}

llvm::Value* Codegen::Impl::coerceToType(llvm::Value* v, llvm::Type* dst) {
    if (!v || v->getType() == dst) return v;
    if (dst->isDoubleTy() && v->getType()->isIntegerTy())
        return builder.CreateSIToFP(v, dblTy, "widen");
    if (dst->isIntegerTy() && v->getType()->isDoubleTy())
        return builder.CreateFPToSI(v, dst, "narrow");
    // Ordinals of different widths meet whenever a char or boolean is stored
    // where an integer was computed, or the reverse.  Zero-extension is the
    // right widening: the narrow ordinals all have non-negative values.
    if (dst->isIntegerTy() && v->getType()->isIntegerTy())
        return builder.CreateZExtOrTrunc(v, dst, "conv");
    return v;
}

// ====================================================================
// EP §6.8.7: Structured value constructor emission
// ====================================================================

// EP §6.4.1: the denoter a value is a value of, with any names it is written
// through followed to the declaration that gives its shape.  Denotes first,
// same reasoning as llvmTypeOfNode's own NamedTypeNode case and
// initialStateShapeOf's identical hop loop: it is what Sema resolved this
// name to where it was WRITTEN, not a flat table rebuilt per procedure.
// typeAliases is a fallback for the node it cannot reach, not the first
// answer to ask.
const TypeNode* Codegen::Impl::denoterOf(const TypeNode* tn) const {
    for (int hops = 0; tn && hops < 32; ++hops) {
        auto* named = llvm::dyn_cast<NamedTypeNode>(tn);
        if (!named) return tn;
        const TypeNode* next = named->Denotes;
        if (!next) {
            auto it = typeAliases.find(toLower(named->Name));
            if (it == typeAliases.end()) return tn;
            next = it->second;
        }
        if (next == tn) return tn;
        tn = next;
    }
    return tn;
}
