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
#include "RangeCheckGuards.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"
#include "SetOps.h"
#include "VarEntry.h"

namespace llvm {
class Module;
class Value;
class Function;
class AllocaInst;
}

class StringCallMarshalling;

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
        SetOps& Sets, StringCallMarshalling& StrCall, RangeCheckGuards& RangeGuards,
        llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty, llvm::PointerType* PtrTy,
        std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
        std::function<llvm::Value*(const plang::ExprNode&, llvm::Type*, bool)> EmitCallArg,
        std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
        std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
        std::function<bool(const std::string&)> IsNestedFunction)
        : Ctx(Ctx), Mod(Mod), B(B), Schema(Schema), SchemaLayout(SchemaLayout),
          Types(Types), SymTab(SymTab), Linkage(Linkage), DbgInfo(DbgInfo),
          Sets(Sets), StrCall(StrCall), RangeGuards(RangeGuards),
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
    /// Issue #647: a procedural-VARIABLE actual relayed into a procedural
    /// PARAMETER formal -- unlike procParamThunk, there is no single
    /// compile-time \p target to wrap (the variable may hold any routine
    /// congruous with \p node by the time this call runs), so this is one
    /// thunk SHARED by every procedural-variable actual of a given
    /// signature rather than one per (target, signature) pair. Built with
    /// \p node's own procParamFnType shape (a leading frame parameter, present
    /// in every procedural-parameter formal's calling convention -- see
    /// procParamFnType's own comment) but repurposes that frame slot to
    /// smuggle across the one thing a flat procedural variable actually has
    /// and a compile-time thunk cannot supply: the real, run-time-only
    /// entry point. pushProcParamArgs hands this thunk as the "entry point"
    /// half of the pair and the variable's own loaded flat pointer as the
    /// "frame" half; this thunk then calls THAT value through \p node's own
    /// procVarFnType shape (no frame of its own -- a procedural variable is
    /// never assigned a nested/capturing routine, Sema::checkRoutineValue),
    /// forwarding every other argument unchanged.
    llvm::Function* procVarRelayThunk(const plang::ProcedureTypeNode& node);
    /// Pushes the (entry point, frame) pair for a procedure/function named
    /// as an actual argument -- or, per issue #647, for a procedural
    /// VARIABLE named as one (wrapped through procVarRelayThunk, its own
    /// loaded flat pointer riding in the frame slot -- see that function's
    /// own comment).
    void pushProcParamArgs(std::vector<llvm::Value*>& args, const plang::ExprNode& arg,
                           const plang::ProcedureTypeNode& node);
    /// Emits a call through procedural parameter \p ve.  Returns null for a
    /// procedural (void) target.
    llvm::Value* emitProcParamCall(const VarEntry& ve,
                                   std::span<const std::unique_ptr<plang::ExprNode>> argExprs);

    /// Turbo procedural VALUES: the LLVM signature a procedural VARIABLE is
    /// called through -- exactly \p node's shape (see procParamFnType), but
    /// with no leading frame parameter, since a procedural variable's
    /// storage is one flat pointer and Sema::checkRoutineValue has already
    /// refused any routine that would need a frame at the assignment that
    /// put it there.
    llvm::FunctionType* procVarFnType(const plang::ProcedureTypeNode& node);
    /// Emits a call through procedural variable \p ve.  Returns null for a
    /// procedural (void) target.  A separate function from emitProcParamCall
    /// rather than a shared one parameterized over "has a frame or not":
    /// ISO §6.6.3.1's procedural-parameter ABI is stable, tested foundation
    /// this feature deliberately builds ALONGSIDE rather than reshapes.
    llvm::Value* emitProcVarCall(const VarEntry& ve,
                                 std::span<const std::unique_ptr<plang::ExprNode>> argExprs);

    /// Turbo procedural VALUES (issue #648): a call through an arbitrary
    /// procedural-typed EXPRESSION -- 'a[i](args)', 'p^(args)' -- rather
    /// than through a named procedural VARIABLE's own VarEntry
    /// (emitProcVarCall) or a procedural PARAMETER's {entry point, frame}
    /// pair (emitProcParamCall). \p calleeAddr is that expression's own
    /// LVALUE ADDRESS (an ordinary flat-pointer slot, loaded the same way
    /// emitProcVarCall reads ve.ptr -- there is no frame, for the identical
    /// reason: Sema::checkRoutineValue never lets a nested/capturing
    /// routine reach a procedural-typed VALUE in the first place, and that
    /// is exactly what this expression's own static type denotes). \p fnTy
    /// is Callee's Sema-resolved Procedure/Function Type. Builds its own
    /// LLVM FunctionType straight from \p fnTy (fnTypeFromSemaType) rather
    /// than procVarFnType's ProcedureTypeNode walk, since there is in
    /// general no single AST ProcedureTypeNode this callee's type was
    /// declared with (it may have been reached through any number of
    /// array/pointer layers) -- Sema::checkIndirectCall has already refused
    /// a \p fnTy with a procedural parameter of its own, so unlike
    /// procVarFnType/emitProcVarCall this never has to build that
    /// {entry point, frame} shape for one of \p fnTy's own parameters.
    /// Returns null for a procedural (void) target.
    llvm::Value* emitIndirectCall(llvm::Value* calleeAddr, const plang::Type& fnTy,
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
    SetOps& Sets;
    /// Issue #299 Phase 2: emitProcParamCall builds its own transient
    /// CGCallMarshal per call (see its own comment) rather than sharing
    /// Codegen::Impl's callMarshal_, so it needs this reference directly,
    /// the same way it already holds Schema/Sets rather than going through
    /// a closure for them.
    StringCallMarshalling& StrCall;
    /// emitProcVarCall's own nil-entry-point guard (issue #646) -- the same
    /// RTE 216 idiom CGProcCall/CGFuncCall/CGFieldAccess/SchemaAccess
    /// already use for every other pointer dereference, reused rather than
    /// reimplemented here.
    RangeCheckGuards& RangeGuards;
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

    /// Issue #647: procVarRelayThunk's own cache, keyed by the OUTER
    /// (procParamFnType) signature alone -- there is no per-target
    /// dimension here the way procParamThunks_ has one, since this thunk
    /// forwards to whatever the frame slot hands it at run time rather than
    /// to a single compile-time Function*, so one thunk per signature
    /// serves every procedural-variable actual of that shape.
    std::map<llvm::FunctionType*, llvm::Function*> procVarRelayThunks_;

    /// Discriminants a schema formal of this denoter takes, or 0 (EP
    /// §6.4.7) -- trivial enough to duplicate rather than bridge, reading
    /// only the denoter's own resolved Sema type.
    unsigned schemaParamDiscCount(const plang::TypeNode* t) const;
    /// The EP string(N) type \p T denotes, or null, looking through a
    /// schema whose body is a string -- likewise trivial and stateless.
    static const plang::Type* varStrTypeOf(const plang::Type* T);
    /// Issue #684: the Turbo string[N] (ShortString) type \p T denotes, or
    /// null -- ClosureAndCallABI's own copy of Codegen::Impl::shortStrTypeOf
    /// (CodeGenImpl.h's Type* overload), trivial and stateless the same way
    /// varStrTypeOf just above already is duplicated here rather than
    /// bridged.
    static const plang::Type* shortStrTypeOf(const plang::Type* T);
    /// True when \p T is a struct-returning string kind (EP string(N), a
    /// schema wrapping one, or Turbo's ShortString) whose call result needs
    /// spilling to a temporary -- the one predicate emitProcParamCall and
    /// emitProcVarCall both consult below, mirroring CGCallMarshal::
    /// spillStructReturnIfNeeded's identical pair of checks on the DIRECT
    /// call path (CGCallMarshal.cpp).  Before this, only varStrTypeOf was
    /// consulted here, so a ShortString function's result reached an
    /// indirect call (through a procedural parameter or a procedural
    /// variable) still shaped as the raw struct value every consumer of a
    /// string expression expects an ADDRESS for instead -- an LLVM IR
    /// verifier failure the direct-call path never had, because
    /// spillStructReturnIfNeeded already checked both kinds.
    static bool needsStructReturnSpill(const plang::Type* T) {
        return varStrTypeOf(T) != nullptr || shortStrTypeOf(T) != nullptr;
    }
    /// emitIndirectCall's own LLVM FunctionType, built directly from \p fnTy
    /// (a Procedure/Function semantic Type) rather than from an AST
    /// ProcedureTypeNode the way procParamFnType/procVarFnType are -- see
    /// emitIndirectCall's own comment for why there is no single such
    /// TypeNode to walk here.  \p fnTy's own Params are known (by
    /// Sema::checkIndirectCall's own refusal) to contain no procedural
    /// parameter of their own, so unlike procVarFnType this never has to
    /// build that nested {entry point, frame} shape.
    llvm::FunctionType* fnTypeFromSemaType(const plang::Type& fnTy);
};
