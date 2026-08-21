// FileVarHelpers.h — ISO §6.6.5.2 file-variable address/type/size helpers.
#pragma once

#include <cstdint>
#include <functional>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "CGSymbolTable.h"
#include "CGTypes.h"
#include "RuntimeFunctionCache.h"

namespace llvm {
class Module;
class Value;
}

namespace plang {
struct ExprNode;
struct TypeNode;
struct Type;
}

class FileVarHelpers {
public:
    FileVarHelpers(llvm::Module& Mod, llvm::IRBuilder<>& B,
                    CGSymbolTable& SymTab, CGTypes& Types, RuntimeFunctionCache& RtFns,
                    llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty, llvm::PointerType* PtrTy,
                    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue)
        : Mod(Mod), B(B), SymTab(SymTab), Types(Types), RtFns(RtFns),
          I64Ty(I64Ty), I8Ty(I8Ty), PtrTy(PtrTy), EmitLValue(std::move(EmitLValue)) {}

    static bool isTextTypeName(const plang::TypeNode* tn);
    /// ISO §6.6.5.2 takes a file-variable, and a variable is anything §6.5
    /// calls one: an element of an array of text is as much a file as a
    /// variable whose name is written on its own.
    bool isFileVar(const plang::ExprNode& e);
    /// The address of a file variable, direct or via lvalue.
    llvm::Value* fileVarPtr(const plang::ExprNode& e);
    const plang::Type* fileTypeOf(const plang::ExprNode& e);
    /// Whether the file holds records or characters -- two unrelated
    /// representations, decided by the denoter rather than a type name.
    bool isTypedBinaryFileVar(const plang::ExprNode& e);
    /// `f^`'s buffer pointer via plang_file_buffer.
    llvm::Value* fileBufferPtr(const plang::ExprNode& fileExpr);
    llvm::Type* getFileElemType(const plang::ExprNode& fileExpr);
    int64_t getFileElemSize(const plang::ExprNode& fileExpr);
    int64_t getFileIndexLow(const plang::ExprNode& fileExpr);

private:
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    CGSymbolTable& SymTab;
    CGTypes& Types;
    RuntimeFunctionCache& RtFns;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
};
