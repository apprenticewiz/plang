// CGExprCore.h — ISO §6.7.1 expression emission: emitExpr (the central
// recursive-descent rvalue dispatcher), emitLValue (its address/lvalue
// counterpart), and spillToTemporary (the shared function-call-result
// addressing helper both use).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGBinaryOps.h"
#include "CGFieldAccess.h"
#include "CGFuncCall.h"
#include "CGIndexAccess.h"
#include "CGLinkage.h"
#include "CGStructuredValue.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "FileVarHelpers.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringRuntime.h"
#include "VarEntry.h"

namespace llvm { class AllocaInst; class Module; class Value; }
namespace plang { struct ExprNode; struct Type; }

class CGExprCore {
public:
    CGExprCore(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
               RuntimeFunctionCache& RtFns, CGSymbolTable& SymTab,
               ClosureAndCallABI& ClosureAbi, CGLinkage& Linkage,
               CGFuncCall& FuncCall, CGBinaryOps& BinaryOps,
               CGIndexAccess& IndexAccess, CGFieldAccess& FieldAccess,
               SetOps& Sets, CGTypes& Types, StringRuntime& Strings,
               SchemaAccess& Schema, CGStructuredValue& StructuredValue,
               FileVarHelpers& FileVars, RangeCheckGuards& RangeGuards,
               llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty,
               llvm::Type* DblTy, llvm::PointerType* PtrTy,
               llvm::AllocaInst*& CurRetAlloca, llvm::Type*& CurRetType,
               std::string& CurFuncName,
               std::unordered_map<std::string, llvm::Value*>& Consts,
               std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
               std::function<const VarEntry*(const std::string&, const plang::Type*)> ResolveImportedVar,
               std::function<llvm::Value*(llvm::Value*)> EnsureI1,
               std::function<llvm::Value*(llvm::Value*)> ToI64,
               std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
               std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic)
        : Ctx(Ctx), Mod(Mod), B(B), RtFns(RtFns), SymTab(SymTab),
          ClosureAbi(ClosureAbi), Linkage(Linkage), FuncCall(FuncCall),
          BinaryOps(BinaryOps), IndexAccess(IndexAccess), FieldAccess(FieldAccess),
          Sets(Sets), Types(Types), Strings(Strings), Schema(Schema),
          StructuredValue(StructuredValue), FileVars(FileVars), RangeGuards(RangeGuards),
          I64Ty(I64Ty), I8Ty(I8Ty), DblTy(DblTy), PtrTy(PtrTy),
          CurRetAlloca(CurRetAlloca), CurRetType(CurRetType), CurFuncName(CurFuncName),
          Consts(Consts),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          ResolveImportedVar(std::move(ResolveImportedVar)),
          EnsureI1(std::move(EnsureI1)), ToI64(std::move(ToI64)),
          ExprIsVarStr(std::move(ExprIsVarStr)),
          ExprStrCapStatic(std::move(ExprStrCapStatic)) {}

    /// Invariant every caller of emitExpr (directly, or indirectly through
    /// the EmitExpr closures threaded into CGBinaryOps, CGControlFlow, and
    /// the rest of CodeGen) must hold: emitExpr may split the CURRENT basic
    /// block into several and leave the IRBuilder's insertion point in a
    /// block other than the one that was current when it was called -- it is
    /// not guaranteed to return with the builder still sitting in whatever
    /// block the caller last set.  That has always been true for EP's
    /// and_then/or_else (CGBinaryOps::emitShortCircuit's two-block-plus-PHI
    /// shape) and, since Turbo's `{$B-}` short-circuiting `and`/`or` reuses
    /// that same shape, now also for an ordinary Boolean `and`/`or` under
    /// Turbo.  A caller that needs "the block this value's control flow came
    /// from" -- most concretely, a PHI's addIncoming predecessor -- MUST
    /// call B.GetInsertBlock() AFTER emitExpr returns, never reuse a
    /// BasicBlock* captured before calling it: the pre-call block may no
    /// longer be the one whose terminator actually falls through to here.
    /// Every current caller already does this (CGBinaryOps::emitShortCircuit
    /// itself re-fetches fromRhs post-call; every other caller either never
    /// builds a PHI at all or, like Codegen::Impl::StackScope, only ever
    /// queries the CURRENT block through the builder's live insertion point
    /// rather than a stashed one) -- this comment makes that already-relied-
    /// upon rule explicit rather than leaving it to be independently
    /// rediscovered by the next feature that makes emitExpr split a block in
    /// a new place.
    llvm::Value* emitExpr(const plang::ExprNode& e);
    /// Returns the POINTER to the storage for an lvalue expression.
    llvm::Value* emitLValue(const plang::ExprNode& e);
    llvm::Value* spillToTemporary(const plang::ExprNode& e);

private:
    // Live activations of emitExpr.  Every recursive re-entry into expression
    // emission -- a binary/unary operand, a call argument, an index/field/
    // deref base -- funnels through emitExpr (directly, or indirectly via the
    // EmitExpr callback threaded into CGBinaryOps and friends), so bounding
    // activations here bounds the whole recursive descent.
    //
    // Sema::checkExpr (see ExprDepthScope/MaxExprDepth in SemaExpr.cpp) already
    // rejects any expression nesting >= 1000 checkExpr activations deep before
    // it ever reaches codegen, so a Sema-accepted expression is nested well
    // under 1000 levels deep by construction. In an ordinary Release/Debug
    // build that is no problem: CodeGen's per-frame stack usage keeps its own
    // practical crash threshold safely above 1000, so MaxExprDepth below is
    // set comfortably higher still -- purely defense-in-depth against a
    // genuine internal inconsistency (Sema and CodeGen disagreeing about what
    // "too deep" means), not expected to fire on any real, Sema-accepted
    // program.
    //
    // Under this project's own ASan+UBSan CI build, though, ASan's much
    // larger per-frame stack usage (redzones, shadow-memory bookkeeping, no
    // tail-call folding) drops CodeGen's *real* crash threshold well BELOW
    // Sema's 1000-term cap -- empirically, on this codebase, a flat
    // `1+1+...+1` chain of as few as ~380 terms crashes CodeGen with a raw
    // SIGSEGV / ASan stack-overflow report under
    // `-DPLANG_SANITIZE=address,undefined` (issue #146), even though Sema
    // accepted it with no diagnostic and the same input compiles fine on a
    // non-sanitized build. A guard above 1000 cannot catch that: the real
    // stack overflow happens first. So under a sanitizer build specifically,
    // MaxExprDepth is instead set well BELOW the measured ~380-term crash
    // floor (with a margin for expression kinds heavier than a plain
    // integer '+' chain), trading "reject a small, unrealistic sliver of
    // deeply-nested-but-legal expressions" for "never let CodeGen's own
    // recursion raw-SIGSEGV" -- turning the crash into the same clean,
    // diagnosable failure (codegenICE, CodegenICE.h) every other invariant
    // violation in codegen produces.
#if defined(__SANITIZE_ADDRESS__) \
    || (defined(__has_feature) && __has_feature(address_sanitizer))
    static constexpr unsigned MaxExprDepth = 200;
#else
    static constexpr unsigned MaxExprDepth = 4000;
#endif
    unsigned                  ExprDepth_{};
    struct ExprDepthScope {
        unsigned& N;
        explicit ExprDepthScope(unsigned& Counter) : N(Counter) { ++N; }
        ~ExprDepthScope() { --N; }
    };

    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    RuntimeFunctionCache& RtFns;
    CGSymbolTable& SymTab;
    ClosureAndCallABI& ClosureAbi;
    CGLinkage& Linkage;
    CGFuncCall& FuncCall;
    CGBinaryOps& BinaryOps;
    CGIndexAccess& IndexAccess;
    CGFieldAccess& FieldAccess;
    SetOps& Sets;
    CGTypes& Types;
    StringRuntime& Strings;
    SchemaAccess& Schema;
    CGStructuredValue& StructuredValue;
    FileVarHelpers& FileVars;
    RangeCheckGuards& RangeGuards;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    llvm::AllocaInst*& CurRetAlloca;
    llvm::Type*& CurRetType;
    std::string& CurFuncName;
    std::unordered_map<std::string, llvm::Value*>& Consts;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<const VarEntry*(const std::string&, const plang::Type*)> ResolveImportedVar;
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, v, true);
    }
};
