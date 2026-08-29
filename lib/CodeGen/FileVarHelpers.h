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

#include "plang/Basic/LangOptions.h"

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
                    const plang::LangOptions& LangOpts,
                    llvm::IntegerType* I64Ty, llvm::IntegerType* I8Ty, llvm::PointerType* PtrTy,
                    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue)
        : Mod(Mod), B(B), SymTab(SymTab), Types(Types), RtFns(RtFns), LangOpts(LangOpts),
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
    /// TP RecSize wiring only: whether `e` is a genuinely UNTYPED file
    /// (`var f: file;`, no `of` clause) rather than `text` -- both have a
    /// null ElemType (see isTypedBinaryFileVar's own "untyped: byte-level"
    /// comment), so ElemType alone cannot tell them apart.  TypeContext
    /// mints `text` as its own singleton (Name "text") and an untyped file
    /// through the ordinary getFile(nullptr, ...) path (Name "file" --
    /// see TypeContext::getFile), so the Type's own Name is what
    /// distinguishes them.  Real Turbo Pascal's RecSize default (128) is an
    /// untyped-file-only concept; a `text` file has no RecSize overload of
    /// Reset/Rewrite at all.
    bool isUntypedFileVar(const plang::ExprNode& e);
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
    const plang::LangOptions& LangOpts;
    llvm::IntegerType* I64Ty;
    llvm::IntegerType* I8Ty;
    llvm::PointerType* PtrTy;
    std::function<llvm::Value*(const plang::ExprNode&)> EmitLValue;
};
