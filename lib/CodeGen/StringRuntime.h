// StringRuntime.h — string-literal interning.
//
// Scope is deliberately narrow: interning only, not the wider string-ABI
// cluster (emitStrAssign, emitStrStore, etc. stay elsewhere and become
// clients of this unit where they need internStrPtr).
#pragma once

#include <functional>
#include <map>
#include <string>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

class StringRuntime {
public:
    /// StrStructTypeOf(cap) must return the { i64, [cap x i8] } struct type
    /// for capacity \p cap -- CGTypes territory (Codegen::Impl::strStructType
    /// today), injected rather than depended on directly so this unit has no
    /// concrete dependency on the type-lowering half of the compiler.
    StringRuntime(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
                  std::function<llvm::StructType*(int64_t)> StrStructTypeOf)
        : Ctx(Ctx), Mod(Mod), B(B), StrStructTypeOf(std::move(StrStructTypeOf)) {}

    llvm::GlobalVariable* internStrGV(const std::string& content);
    llvm::Value* internStrPtr(const std::string& content);
    /// A read-only { length, bytes } string struct holding \p content, which
    /// is the shape every string value is passed around as.
    llvm::Constant* internStrStruct(const std::string& content);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    std::function<llvm::StructType*(int64_t)> StrStructTypeOf;

    std::map<std::string, llvm::GlobalVariable*> StrGVs;
    // Interned { length, bytes } string structs, for constants whose value is
    // used where a string variable would be.
    std::map<std::string, llvm::GlobalVariable*> StructGVs;
};
