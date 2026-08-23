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

    llvm::Value* emitExpr(const plang::ExprNode& e);
    /// Returns the POINTER to the storage for an lvalue expression.
    llvm::Value* emitLValue(const plang::ExprNode& e);
    llvm::Value* spillToTemporary(const plang::ExprNode& e);

private:
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
