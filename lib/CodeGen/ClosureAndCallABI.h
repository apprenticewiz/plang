// ClosureAndCallABI.h — ISO §6.6.3.1 procedural-parameter ABI (the
// {entry point, frame} pair, its uniform-signature thunk, and calling
// through it) plus EP §6.7.3.7 conformant-array argument marshalling,
// colocated because a procedural-parameter call marshals its own
// conformant/schema/proc arguments the same way an ordinary call does.
//
// buildStaticLinkFrame -- the static-link frame a call BUILDS -- stays on
// Impl, deliberately: it resolves the closure-capture loop's own state
// (nestedFunctions_/funcOuterVarNames_/funcOuterVarDepths_/
// outerVarBindings/findVarInFunctionScope), the standing extra-caution
// zone this project treats with real-debugger verification, not just
// tests. Reached here only through the BuildStaticLinkFrame closure below.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGDebugInfo.h"
#include "CGLinkage.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"
#include "VarEntry.h"

namespace llvm {
class Module;
class Value;
class Function;
class AllocaInst;
}

namespace plang {
struct ExprNode;
struct ProcedureTypeNode;
struct TypeNode;
struct Type;
}

class ClosureAndCallABI {
public:
    ClosureAndCallABI(
        llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
        SchemaAccess& Schema, SchemaLayoutEngine& SchemaLayout, CGTypes& Types,
        CGSymbolTable& SymTab, CGLinkage& Linkage, CGDebugInfo& DbgInfo,
        llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
        std::function<llvm::Value*(const plang::ExprNode&, llvm::Type*, bool)> EmitCallArg,
        std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
        std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
        std::function<bool(const std::string&)> IsNestedFunction)
        : Ctx(Ctx), Mod(Mod), B(B), Schema(Schema), SchemaLayout(SchemaLayout),
          Types(Types), SymTab(SymTab), Linkage(Linkage), DbgInfo(DbgInfo),
          I32Ty(I32Ty), I64Ty(I64Ty), PtrTy(PtrTy),
          EmitLValue(std::move(EmitLValue)), EmitCallArg(std::move(EmitCallArg)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          BuildStaticLinkFrame(std::move(BuildStaticLinkFrame)),
          IsNestedFunction(std::move(IsNestedFunction)) {}

    /// The { entry point, frame } cell a procedural parameter is held in.
    llvm::StructType* procPairTy() const { return llvm::StructType::get(Ctx, {PtrTy, PtrTy}); }
    void storeProcPair(llvm::Value* cell, llvm::Value* fn, llvm::Value* frame);
    /// Reads a closure cell back as (entry point, frame).
    std::pair<llvm::Value*, llvm::Value*> loadProcPair(llvm::Value* cell);

    /// Pushes an actual for a conformant array formal: the array, then a
    /// lo/hi pair per dimension (EP §6.7.3.7).
    void pushConformantArgs(std::vector<llvm::Value*>& args, const plang::ExprNode& arg,
                            size_t dims);

    /// The LLVM signature a procedural parameter is called through.
    llvm::FunctionType* procParamFnType(const plang::ProcedureTypeNode& node);
    /// Wrapper around \p target presenting the uniform procedural-parameter
    /// signature, forwarding the frame only if \p target actually captures
    /// anything.
    llvm::Function* procParamThunk(llvm::Function* target,
                                   const plang::ProcedureTypeNode& node);
    /// Pushes the (entry point, frame) pair for a procedure/function named
    /// as an actual argument.
    void pushProcParamArgs(std::vector<llvm::Value*>& args, const plang::ExprNode& arg,
                           const plang::ProcedureTypeNode& node);
    /// Emits a call through procedural parameter \p ve.  Returns null for a
    /// procedural (void) target.
    llvm::Value* emitProcParamCall(const VarEntry& ve,
                                   std::span<const std::unique_ptr<plang::ExprNode>> argExprs);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    SchemaAccess& Schema;
    SchemaLayoutEngine& SchemaLayout;
    CGTypes& Types;
    CGSymbolTable& SymTab;
    CGLinkage& Linkage;
    CGDebugInfo& DbgInfo;
    llvm::IntegerType* I32Ty;
    llvm::IntegerType* I64Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(const plang::ExprNode&, llvm::Type*, bool)> EmitCallArg;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    /// Deliberately not absorbed -- see this header's own top comment.
    std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame;
    /// procParamThunk's own membership test into nestedFunctions_ --
    /// distinct from BuildStaticLinkFrame's, which resolves the whole
    /// capture loop; this is just "does this target need a frame at all."
    std::function<bool(const std::string&)> IsNestedFunction;

    /// Uniform-signature thunks, keyed by the callee they wrap and the
    /// signature they present it through; zero external touches, so this
    /// stays private rather than referenced.
    std::map<std::pair<llvm::Function*, llvm::FunctionType*>, llvm::Function*>
        procParamThunks_;

    /// Discriminants a schema formal of this denoter takes, or 0 (EP
    /// §6.4.7) -- trivial enough to duplicate rather than bridge, reading
    /// only the denoter's own resolved Sema type.
    unsigned schemaParamDiscCount(const plang::TypeNode* t) const;
    /// The EP string(N) type \p T denotes, or null, looking through a
    /// schema whose body is a string -- likewise trivial and stateless.
    static const plang::Type* varStrTypeOf(const plang::Type* T);
};
