// CGFuncCall.h — call-expression emission: the built-in function dispatch
// chain (math/complex/file-status/ordinal/EP §6.7.5.4 string functions) and
// the tail call to a user-declared function (ISO §6.6.5).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGLinkage.h"
#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "ClosureAndCallABI.h"
#include "ComplexOps.h"
#include "FileVarHelpers.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "SetOps.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Module; class Value; }
namespace plang {
struct CallExpr; struct ExprNode; struct ProcedureTypeNode;
}

class CGFuncCall {
public:
    CGFuncCall(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
               RuntimeFunctionCache& RtFns, SetOps& Sets, ComplexOps& Complex,
               FileVarHelpers& FileVars, CGTypes& Types, SchemaAccess& Schema,
               StringRuntime& Strings, StringCallMarshalling& StrCall,
               CGLinkage& Linkage, CGSymbolTable& SymTab,
               ClosureAndCallABI& ClosureAbi, RangeCheckGuards& RangeGuards,
               llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty,
               llvm::Type* DblTy, llvm::PointerType* PtrTy,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
               std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
               std::function<llvm::Value*(llvm::Value*)> ToDouble,
               std::function<llvm::Value*(llvm::Value*)> ToI64,
               std::function<llvm::Value*(llvm::Value*)> EnsureI1,
               std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
               std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca,
               std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame,
               std::function<size_t(const std::string&, size_t)> ConformantDimsOf,
               std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf,
               std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg,
               std::function<bool(const std::string&, size_t)> ParamIsByRef,
               std::function<bool(const plang::ExprNode&)> ExprIsVarStr,
               std::function<bool(const plang::ExprNode&)> ExprIsCharStr,
               std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen,
               std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic,
               std::function<bool(const plang::ExprNode&)> ExprIsShortStr,
               std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap)
        : Ctx(Ctx), Mod(Mod), B(B), RtFns(RtFns), Sets(Sets), Complex(Complex),
          FileVars(FileVars), Types(Types), Schema(Schema), Strings(Strings),
          StrCall(StrCall), Linkage(Linkage), SymTab(SymTab), ClosureAbi(ClosureAbi),
          RangeGuards(RangeGuards),
          I64Ty(I64Ty), I8Ty(I8Ty), DblTy(DblTy), PtrTy(PtrTy),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToDouble(std::move(ToDouble)), ToI64(std::move(ToI64)), EnsureI1(std::move(EnsureI1)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          CreateDynStrAlloca(std::move(CreateDynStrAlloca)),
          BuildStaticLinkFrame(std::move(BuildStaticLinkFrame)),
          ConformantDimsOf(std::move(ConformantDimsOf)),
          ParamSetBaseOf(std::move(ParamSetBaseOf)),
          ProcParamArg(std::move(ProcParamArg)), ParamIsByRef(std::move(ParamIsByRef)),
          ExprIsVarStr(std::move(ExprIsVarStr)), ExprIsCharStr(std::move(ExprIsCharStr)),
          ExprCharStrLen(std::move(ExprCharStrLen)), ExprStrCapStatic(std::move(ExprStrCapStatic)),
          ExprIsShortStr(std::move(ExprIsShortStr)), ExprShortStrCap(std::move(ExprShortStrCap)) {}

    llvm::Value* emitCallExpr(const plang::CallExpr& e);
    llvm::Value* emitUserFuncCall(const plang::CallExpr& e);

    /// The built-in dispatch chain that used to be emitCallExpr's whole body,
    /// factored out so a call site with no CallExpr of its own -- Turbo
    /// `{$X+}`'s "a built-in function may be called as a statement, its
    /// result discarded" -- can still reach it.  CGProcCall::emitCallStmt's
    /// tail (a builtin call that matched none of the required-PROCEDURE
    /// names it dispatches by spelling) is the one other caller: every
    /// Proc-kind row in Builtins.def already has its own named arm there, so
    /// reaching that tail with ResolvedBuiltin already known non-None means
    /// this, a builtin FUNCTION, and Sema having allowed it through only
    /// under {$X+}.  Args is a span, not an owned vector, so CGProcCall can
    /// pass CallStmt's own Args straight through with nothing to move or
    /// copy -- s.Args is not `mutable`, and this project's convention
    /// reserves const_cast-around-constness for fields that ARE (see
    /// ExprNode::ResolvedType's own comment).
    ///
    /// Returns nullptr only for a builtin name every arm below fails to
    /// match, which Builtins.def and this dispatch's own completeness
    /// should make unreachable -- emitCallExpr still falls back to
    /// emitUserFuncCall the same way it always has, and CGProcCall's own
    /// caller has no such fallback and reports a codegen ICE instead.
    llvm::Value* emitBuiltinCall(const std::string& Name,
                                  std::span<const std::unique_ptr<plang::ExprNode>> Args,
                                  plang::SourceLocation Loc);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    RuntimeFunctionCache& RtFns;
    SetOps& Sets;
    ComplexOps& Complex;
    FileVarHelpers& FileVars;
    CGTypes& Types;
    SchemaAccess& Schema;
    StringRuntime& Strings;
    StringCallMarshalling& StrCall;
    CGLinkage& Linkage;
    CGSymbolTable& SymTab;
    ClosureAndCallABI& ClosureAbi;
    RangeCheckGuards& RangeGuards;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToDouble;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<llvm::Value*(llvm::Value*)> EnsureI1;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<llvm::Value*(llvm::Value*, const std::string&)> CreateDynStrAlloca;
    std::function<llvm::Value*(const std::string&)> BuildStaticLinkFrame;
    std::function<size_t(const std::string&, size_t)> ConformantDimsOf;
    std::function<std::optional<int64_t>(const std::string&, size_t)> ParamSetBaseOf;
    std::function<const plang::ProcedureTypeNode*(const std::string&, size_t)> ProcParamArg;
    std::function<bool(const std::string&, size_t)> ParamIsByRef;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
    std::function<bool(const plang::ExprNode&)> ExprIsCharStr;
    std::function<int64_t(const plang::ExprNode&)> ExprCharStrLen;
    std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic;
    /// Turbo string[N]'s own predicate -- see exprIsShortStr's doc comment
    /// (CodeGenImpl.h).  Used only by emitUserFuncCall's struct-return spill;
    /// the EP-only builtin string functions (Length/Substr/Trim/EQ and
    /// friends, all gated `EP` in Builtins.def) never see a ShortString
    /// argument at all, so they need no ShortString capacity query to match
    /// ExprStrCapStatic's VarString one.
    std::function<bool(const plang::ExprNode&)> ExprIsShortStr;
    /// Turbo string[N]'s own capacity query -- the ShortString sibling of
    /// ExprStrCapStatic, needed by the Turbo System-unit string routines
    /// (Copy/Pos/Concat/StringOfChar/UpCase, CGFuncCall.cpp) to size a
    /// result temporary or shape a ShortString operand the same way
    /// CGBinaryOps' own local sstrOperand lambda already does for `+`/
    /// comparison.  See exprShortStrCap's own doc comment (CodeGenImpl.h).
    std::function<int64_t(const plang::ExprNode&)> ExprShortStrCap;

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, v, true);
    }
};
