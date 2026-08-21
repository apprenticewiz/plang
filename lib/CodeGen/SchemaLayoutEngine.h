// SchemaLayoutEngine.h — EP §6.4.7 run-time layout, for a schema body whose
// extent a discriminant fixes.
//
// The differential-oracle counterpart to the static layout engine (still in
// CodeGenTypes.cpp / Codegen::Impl, pending its own CGTypes extraction):
// this walk and the static one are independently computed and must never be
// merged into one implementation, only cross-checked explicitly.
//
// Call the *Of methods under an RtDiscScope for the object being laid out:
// every extent in the body is a closed form over the discriminants BY INDEX,
// evaluated against that object's. A subtree that reads no discriminant
// folds to a constant.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "plang/AST/AstBase.h"

#include "RuntimeFunctionCache.h"
#include "SchemaTypeRegistry.h"

namespace plang {
struct ArrayTypeNode;
struct FieldDecl;
struct RecordTypeNode;
struct TypeNode;
struct VariantPart;
}

class SchemaLayoutEngine {
public:
    /// LlvmTypeOfNode/ArrayIndexRangeFn are narrow closures into CGTypes
    /// territory (Codegen::Impl::llvmTypeOfNode/arrayIndexRange today, not
    /// yet its own class) -- the "this subtree does not vary" fallback path
    /// every rt* method bottoms out at.
    SchemaLayoutEngine(
        llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
        SchemaTypeRegistry& SchemaTypes, RuntimeFunctionCache& RtFns,
        std::function<llvm::Type*(const plang::TypeNode&)> LlvmTypeOfNode,
        std::function<std::optional<std::pair<int64_t, int64_t>>(const plang::ArrayTypeNode&)> ArrayIndexRangeFn)
        : Ctx(Ctx), Mod(Mod), B(B), SchemaTypes(SchemaTypes), RtFns(RtFns),
          LlvmTypeOfNode(std::move(LlvmTypeOfNode)),
          ArrayIndexRangeFn(std::move(ArrayIndexRangeFn)) {}

    /// R3: makes an object's discriminants the ones extent forms are
    /// evaluated against, for as long as the guard lives. Restores the
    /// PREVIOUS discriminants rather than clearing them, so a nested walk
    /// doesn't leave the outer one pointing at the inner object's.
    struct RtDiscScope {
        SchemaLayoutEngine& E;
        const std::vector<llvm::Value*>* Prev;
        RtDiscScope(SchemaLayoutEngine& E, const std::vector<llvm::Value*>& D)
            : E(E), Prev(E.rtDiscs_) { E.rtDiscs_ = &D; }
        ~RtDiscScope() { E.rtDiscs_ = Prev; }
        RtDiscScope(const RtDiscScope&) = delete;
        RtDiscScope& operator=(const RtDiscScope&) = delete;
    };

    uint64_t     rtAlignOfTypeNode(const plang::TypeNode* tn);
    llvm::Value* rtSizeOfTypeNode(const plang::TypeNode* tn);
    /// The index bounds of \p at as run-time values.  The only place that
    /// answers this, so that the run-time walk and the static layout cannot
    /// disagree about how many elements an array has.
    std::optional<std::pair<llvm::Value*, llvm::Value*>>
    rtIndexBounds(const plang::ArrayTypeNode& at);
    llvm::Value* rtFieldOffset(const plang::RecordTypeNode& rt, const std::string& field);
    llvm::Value* rtWalkFields(const std::vector<plang::FieldDecl>& fields,
                              llvm::Value* off, bool packed,
                              const std::string* stopAt, bool* found);
    llvm::Value* rtWalkVariant(const plang::VariantPart& vp, llvm::Value* off,
                               bool packed, const std::string* stopAt,
                               bool* found, bool nested = false);
    uint64_t     rtVariantRunAlign(const plang::VariantPart& vp);
    uint64_t     rtVariantAlign(const plang::VariantPart& vp);
    llvm::Value* alignUpV(llvm::Value* v, uint64_t align);
    /// R3: evaluate a closed extent form against the discriminants this
    /// object carries.
    llvm::Value* emitExtentForm(const plang::ExtentForm& F,
                                const std::vector<llvm::Value*>& discs);
    /// A denoter's extent as a value, from its closed form when it has one,
    /// against the AMBIENT discriminants -- correct only inside the
    /// run-time layout walk, entered between an RtDiscScope and its end.
    llvm::Value* extentOf(const std::optional<plang::ExtentForm>& F) {
        return (F && rtDiscs_) ? emitExtentForm(*F, *rtDiscs_) : nullptr;
    }
    /// Bytes of discriminant header in front of a schema body.
    uint64_t schemaHeaderBytes(const plang::Type& schema);
    /// R3: a denoter's low and high extents evaluated against \p discs, the
    /// discriminants of the object it was reached through, so no identifier
    /// in it is ever resolved in the procedure doing the access. Absent when
    /// Sema recorded no form, which outside a schema body is the ordinary
    /// case. Takes the discriminants directly rather than a SchemaRef (that
    /// type belongs to schema value/access-path resolution, not layout), so
    /// this unit has zero dependency on it.
    std::optional<std::pair<llvm::Value*, llvm::Value*>>
    boundsOfDenoter(const plang::TypeNode& D, std::span<llvm::Value* const> discs);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    SchemaTypeRegistry& SchemaTypes;
    RuntimeFunctionCache& RtFns;
    std::function<llvm::Type*(const plang::TypeNode&)> LlvmTypeOfNode;
    std::function<std::optional<std::pair<int64_t, int64_t>>(const plang::ArrayTypeNode&)> ArrayIndexRangeFn;

    /// The discriminants the run-time layout walk is working against, set by
    /// RtDiscScope.  The walk is always entered under one, so this is live
    /// exactly where a form may be evaluated.
    const std::vector<llvm::Value*>* rtDiscs_{nullptr};

    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(v), true);
    }
};
