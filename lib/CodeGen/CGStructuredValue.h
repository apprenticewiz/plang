// CGStructuredValue.h — EP §6.8.7: a typed value constructor
// (array/record/set).
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

#include "CGTypes.h"
#include "SetOps.h"

namespace llvm { class Module; class Value; class AllocaInst; }
namespace plang {
struct StructuredValueExpr; struct ExprNode; struct TypeNode; struct RecordTypeNode;
}

class CGStructuredValue {
public:
    CGStructuredValue(llvm::Module& Mod, llvm::IRBuilder<>& B,
                       CGTypes& Types, SetOps& Sets,
                       std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases,
                       std::unordered_map<std::string, llvm::Value*>& Consts,
                       llvm::IntegerType* I8Ty, llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty,
                       std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                       std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType,
                       std::function<const plang::TypeNode*(const plang::TypeNode*)> InitialStateShapeOf,
                       std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca)
        : Mod(Mod), B(B), Types(Types), Sets(Sets),
          TypeAliases(TypeAliases), Consts(Consts),
          I8Ty(I8Ty), I32Ty(I32Ty), I64Ty(I64Ty),
          EmitExpr(std::move(EmitExpr)), CoerceToType(std::move(CoerceToType)),
          InitialStateShapeOf(std::move(InitialStateShapeOf)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)) {}

    llvm::Value* emitStructuredValue(const plang::StructuredValueExpr& e,
                                      const plang::TypeNode* denoter = nullptr);

private:
    /// The denoter written for a named field, looked for among the fixed
    /// fields and then through the variants, which declare fields of their
    /// own.
    static const plang::TypeNode* fieldDenoter(const plang::RecordTypeNode& rtn,
                                                std::string_view name);

    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    CGTypes& Types;
    SetOps& Sets;
    std::unordered_map<std::string, const plang::TypeNode*>& TypeAliases;
    std::unordered_map<std::string, llvm::Value*>& Consts;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I32Ty;
    llvm::IntegerType* I64Ty;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(llvm::Value*, llvm::Type*)> CoerceToType;
    std::function<const plang::TypeNode*(const plang::TypeNode*)> InitialStateShapeOf;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
};
