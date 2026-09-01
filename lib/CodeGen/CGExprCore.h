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

#include "plang/Basic/StackHeadroom.h"

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
#include "OrdinalSignedness.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringRuntime.h"
#include "VarEntry.h"

namespace llvm { class AllocaInst; class Module; class Value; }
namespace plang { struct ExprNode; struct Type; struct TypeCastExpr; }

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
               std::function<llvm::Value*(llvm::Value*, bool)> ToI64,
               std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
               std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic,
               std::function<llvm::Value*(llvm::Value*, llvm::Type*, bool)> CoerceToType,
               std::function<bool(const plang::ExprNode&)> ExprIsShortStr)
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
          ExprStrCapStatic(std::move(ExprStrCapStatic)),
          CoerceToType(std::move(CoerceToType)),
          ExprIsShortStr(std::move(ExprIsShortStr)) {}

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
    /// Turbo VALUE typecast rvalue emission: TypeName(expr) read as a value.
    /// See emitExpr's TypeCastExpr case for which of the two strategies
    /// (numeric conversion vs. bit-for-bit reinterpretation) applies.
    llvm::Value* emitTypeCastValue(const plang::TypeCastExpr& e);

    // Reference point the stack-headroom check below (see MaxExprDepth's own
    // comment) measures usage from -- the same role Parser::StackBaseline and
    // Sema::StackBaseline play for their own guards (issue #556). An in-class
    // default member initializer, rather than a constructor parameter, is
    // enough here (unlike Parser's/Sema's own StackBaseline, threaded through
    // an explicit constructor for documentation's sake) since it is still
    // evaluated fresh at every CGExprCore construction -- this class has
    // exactly one constructor, so there is no risk of a second one leaving it
    // stale -- and this avoids adding yet another parameter to the already
    // very long parameter list just below.
    std::uintptr_t             StackBaseline_ = plang::captureStackBaseline();

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
    //
    // Neither of those defenses is a live stack-headroom check, though: both
    // are term-count ceilings, tuned against a NORMAL-sized (multi-MiB, or
    // ASan-inflated-but-still-multi-MiB) stack. Under a small but real
    // platform stack budget instead (a constrained container, a hardened
    // deployment, a fuzzing worker -- issue #556), a `**` chain in the
    // 500-1000 term range crashes CodeGen with a raw SIGSEGV in emitBinary/
    // emitExpr's mutual recursion well under BOTH ceilings above -- confirmed
    // via gdb, `ulimit -s 1024`. So plang::stackNearlyExhausted (Basic/
    // StackHeadroom.h, generalized from Parser::parsePower's own guard) is
    // checked alongside the term count below, the same way Sema's own
    // checkExpr needs one for the identical reason (SemaExpr.cpp).
    // codegenICE never returns, so unlike Parser's/Sema's own guards there is
    // no "already reported" latch to worry about ordering a Guard around --
    // the process exits on the very statement that detects either ceiling.
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
    /// The bool is the operand's actual Sema-resolved Type::IsSigned; see
    /// CGBinaryOps.h's identical member for the fuller comment.  Used by
    /// emitExpr's SubstringExpr case (`s[i..j]` used as a value) for Low/
    /// High, each with exprIsSigned(x) (OrdinalSignedness.h) for its own x.
    std::function<llvm::Value*(llvm::Value*, bool)> ToI64;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;
    /// Codegen::Impl::coerceToType: widens/narrows/converts an ordinal or
    /// real value to a destination LLVM type.  Used by a Turbo VALUE
    /// typecast's rvalue emission (see emitExpr's TypeCastExpr case) -- see
    /// its own definition (CodeGenExprs.cpp) for exactly what it does.  The
    /// bool is the SOURCE operand's own Sema-resolved signedness (again
    /// exprIsSigned(n.Operand)), the identical srcSigned parameter every
    /// other CoerceToType caller supplies.
    std::function<llvm::Value*(llvm::Value*, llvm::Type*, bool)> CoerceToType;
    /// Turbo string[N]'s own predicate, the sibling ExprIsVarStr has none of
    /// -- see exprIsShortStr's doc comment (CodeGenImpl.h) for why ShortString
    /// needs no capacity-query closure the way ExprStrCapStatic exists for
    /// VarString (a ShortString's capacity is always a compile-time constant,
    /// read directly off ResolvedType wherever it's needed).
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, v, true);
    }
};
