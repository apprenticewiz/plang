// CGFuncCall.h — call-expression emission: the built-in function dispatch
// chain (math/complex/file-status/ordinal/EP §6.7.5.4 string functions) and
// the tail call to a user-declared function (ISO §6.6.5).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
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
               std::function<int64_t(const plang::ExprNode&)> ExprStrCapStatic)
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
          ExprCharStrLen(std::move(ExprCharStrLen)), ExprStrCapStatic(std::move(ExprStrCapStatic)) {}

    llvm::Value* emitCallExpr(const plang::CallExpr& e);
    llvm::Value* emitUserFuncCall(const plang::CallExpr& e);

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

    llvm::Constant* i64c(int64_t v) const {
        return llvm::ConstantInt::get(I64Ty, v, true);
    }
};
