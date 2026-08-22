// CGWith.h — EP §6.8.3.10: with r1, r2, ... do stmt.
#pragma once

#include <functional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"

namespace llvm { class Value; class AllocaInst; }
namespace plang { struct WithStmt; struct ExprNode; struct StmtNode; struct TypeNode; }

class CGWith {
public:
    CGWith(llvm::IRBuilder<>& B,
           SchemaAccess& Schema, SchemaLayoutEngine& SchemaLayout,
           CGTypes& Types, CGSymbolTable& SymTab,
           llvm::IntegerType* I8Ty, llvm::IntegerType* I32Ty, llvm::IntegerType* I64Ty,
           std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
           std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca,
           std::function<void(const plang::StmtNode*)> EmitStmt,
           std::function<const plang::TypeNode*(const plang::TypeNode*)> PeelPackedNode)
        : B(B), Schema(Schema), SchemaLayout(SchemaLayout), Types(Types), SymTab(SymTab),
          I8Ty(I8Ty), I32Ty(I32Ty), I64Ty(I64Ty),
          EmitLValue(std::move(EmitLValue)),
          CreateEntryAlloca(std::move(CreateEntryAlloca)),
          EmitStmt(std::move(EmitStmt)),
          PeelPackedNode(std::move(PeelPackedNode)) {}

    void emitWith(const plang::WithStmt& s);

private:
    llvm::IRBuilder<>& B;
    SchemaAccess& Schema;
    SchemaLayoutEngine& SchemaLayout;
    CGTypes& Types;
    CGSymbolTable& SymTab;
    llvm::IntegerType* I8Ty;
    llvm::IntegerType* I32Ty;
    llvm::IntegerType* I64Ty;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> CreateEntryAlloca;
    std::function<void(const plang::StmtNode*)> EmitStmt;
    std::function<const plang::TypeNode*(const plang::TypeNode*)> PeelPackedNode;
};
