// CGFieldAccess.h — record field access and pointer dereference:
// r.field, p^.field, p^ (ISO §6.4.3.3/§6.5.5, EP §6.4.7 schema fields).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Alignment.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "FileVarHelpers.h"
#include "RangeCheckGuards.h"
#include "SchemaAccess.h"

namespace llvm { class Value; }
namespace plang {
struct ExprNode; struct FieldExpr; struct DerefExpr; struct TypeNode; struct Type;
}

class CGFieldAccess {
public:
    CGFieldAccess(llvm::IRBuilder<>& B,
                  CGTypes& Types, SchemaAccess& Schema, CGSymbolTable& SymTab,
                  FileVarHelpers& FileVars, RangeCheckGuards& RangeGuards,
                  std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases,
                  llvm::IntegerType* I8Ty, llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty,
                  std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                  std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue)
        : B(B), Types(Types), Schema(Schema), SymTab(SymTab), FileVars(FileVars),
          RangeGuards(RangeGuards), TypeAliases(TypeAliases),
          I8Ty(I8Ty), I32Ty(I32Ty), I64Ty(I64Ty),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)) {}

    llvm::StructType* resolveRecordStructType(const plang::FieldExpr& e);
    llvm::Type* fieldLlvmType(const plang::FieldExpr& e);
    llvm::Value* emitFieldGEP(const plang::FieldExpr& e);
    std::optional<llvm::Align> packedAccessAlign(const plang::ExprNode& e);
    llvm::Value* emitFieldLoad(const plang::FieldExpr& e);
    llvm::Value* emitDerefLoad(const plang::DerefExpr& e);

private:
    llvm::IRBuilder<>& B;
    CGTypes& Types;
    SchemaAccess& Schema;
    CGSymbolTable& SymTab;
    FileVarHelpers& FileVars;
    RangeCheckGuards& RangeGuards;
    std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I32Ty;
    llvm::IntegerType* I64Ty;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
};
