// CGPackUnpack.h — ISO §6.7.5.4 transfer procedures: pack/unpack.
#pragma once

#include <functional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "RangeCheckGuards.h"
#include "SchemaAccess.h"
#include "SchemaLayoutEngine.h"

namespace llvm { class Module; class Value; }
namespace plang { struct CallStmt; struct ExprNode; struct TypeNode; }

class CGPackUnpack {
public:
    CGPackUnpack(llvm::Module& Mod, llvm::IRBuilder<>& B,
                 CGSymbolTable& SymTab, SchemaAccess& Schema,
                 SchemaLayoutEngine& SchemaLayout, CGTypes& Types,
                 RangeCheckGuards& RangeGuards,
                 llvm::IntegerType* I64Ty,
                 std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr,
                 std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue,
                 std::function<llvm::Value*(llvm::Value*)> ToI64,
                 std::function<const plang::TypeNode*(const plang::TypeNode*)> PeelPackedNode)
        : Mod(Mod), B(B), SymTab(SymTab), Schema(Schema), SchemaLayout(SchemaLayout),
          Types(Types), RangeGuards(RangeGuards), I64Ty(I64Ty),
          EmitExpr(std::move(EmitExpr)), EmitLValue(std::move(EmitLValue)),
          ToI64(std::move(ToI64)), PeelPackedNode(std::move(PeelPackedNode)) {}

    void emitPackUnpack(const plang::CallStmt& s, bool isPack);

private:
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    CGSymbolTable& SymTab;
    SchemaAccess& Schema;
    SchemaLayoutEngine& SchemaLayout;
    CGTypes& Types;
    RangeCheckGuards& RangeGuards;
    llvm::IntegerType* I64Ty;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitExpr;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    std::function<const plang::TypeNode*(const plang::TypeNode*)> PeelPackedNode;
};
