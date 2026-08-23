// CGIndexAccess.h — array indexing: a[i] (ISO §6.5.3.2), including EP
// §6.7.3.7 conformant arrays, EP §6.4.7 schema arrays, and EP §6.5.3.2
// string-component indexing.
#pragma once

#include <cstdint>
#include <functional>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "SchemaAccess.h"
#include "StringCallMarshalling.h"
#include "StringRuntime.h"

namespace llvm { class Value; }
namespace plang { struct ExprNode; struct IndexExpr; struct TypeNode; struct ArrayTypeNode; }

class CGIndexAccess {
public:
    CGIndexAccess(llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                  SchemaAccess& Schema, StringCallMarshalling& StrCall,
                  RangeCheckGuards& RangeGuards, StringRuntime& Strings,
                  RuntimeFunctionCache& RtFns, CGSymbolTable& SymTab, CGTypes& Types,
                  llvm::IntegerType* I8Ty, llvm::IntegerType* I64Ty,
                  std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                  std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
                  std::function<llvm::Value*(llvm::Value*)> ToI64,
                  std::function<const plang::TypeNode*(const plang::TypeNode*)> DenoterOf,
                  std::function<bool(const plang::ExprNode&)> ExprIsVarStr)
        : Ctx(Ctx), B(B), Schema(Schema), StrCall(StrCall), RangeGuards(RangeGuards),
          Strings(Strings), RtFns(RtFns), SymTab(SymTab), Types(Types),
          I8Ty(I8Ty), I64Ty(I64Ty),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), DenoterOf(std::move(DenoterOf)),
          ExprIsVarStr(std::move(ExprIsVarStr)) {}

    llvm::Value* emitConformantElemPtr(const plang::IndexExpr& e);
    llvm::Value* emitIndexGEP(const plang::IndexExpr& e);
    llvm::Value* emitIndexLoad(const plang::IndexExpr& e);

private:
    /// The lower bound of an array type's index, from its bounds or Sema.
    int64_t arrayIndexLow(const plang::ArrayTypeNode& n) const;

    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    SchemaAccess& Schema;
    StringCallMarshalling& StrCall;
    RangeCheckGuards& RangeGuards;
    StringRuntime& Strings;
    RuntimeFunctionCache& RtFns;
    CGSymbolTable& SymTab;
    CGTypes& Types;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I64Ty;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<const plang::TypeNode*(const plang::TypeNode*)> DenoterOf;
    std::function<bool(const plang::ExprNode&)> ExprIsVarStr;
};
