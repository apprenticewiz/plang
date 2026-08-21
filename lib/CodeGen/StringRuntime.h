// StringRuntime.h — string-literal interning plus the raw EP string-ABI
// primitives (the { length, bytes } struct layout and the plang_str_*
// runtime calls that read/write it).
//
// The wider string-ABI cluster (emitCallArg/emitStrAddr/emitStrStore and
// friends) stays elsewhere -- real logic with its own emitExpr/emitLValue
// dependencies, a client of this unit rather than part of it, exactly as
// this header's own comment always said it would be.
#pragma once

#include <functional>
#include <initializer_list>
#include <map>
#include <string>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "RuntimeFunctionCache.h"

class StringRuntime {
public:
    /// StrStructTypeOf(cap) must return the { i64, [cap x i8] } struct type
    /// for capacity \p cap -- CGTypes territory (Codegen::Impl::strStructType
    /// today), injected rather than depended on directly so this unit has no
    /// concrete dependency on the type-lowering half of the compiler.
    StringRuntime(llvm::LLVMContext& Ctx, llvm::Module& Mod, llvm::IRBuilder<>& B,
                  RuntimeFunctionCache& RtFns,
                  std::function<llvm::StructType*(int64_t)> StrStructTypeOf)
        : Ctx(Ctx), Mod(Mod), B(B), RtFns(RtFns),
          StrStructTypeOf(std::move(StrStructTypeOf)) {}

    llvm::GlobalVariable* internStrGV(const std::string& content);
    llvm::Value* internStrPtr(const std::string& content);
    /// A read-only { length, bytes } string struct holding \p content, which
    /// is the shape every string value is passed around as.
    llvm::Constant* internStrStruct(const std::string& content);

    /// Declares/looks up a plang_str_* runtime function.
    llvm::Function* getStrFn(const std::string& name, llvm::Type* retTy,
                             std::initializer_list<llvm::Type*> argTys);
    /// The i64 length field of a { length, bytes } string struct at \p strPtr.
    llvm::Value* strLoadLen(llvm::Value* strPtr);
    /// The address of the bytes following the length field.
    llvm::Value* strDataPtr(llvm::Value* strPtr);
    void emitStrAssign(llvm::Value* dst, llvm::Value* capDst,
                       llvm::Value* src, llvm::Value* capSrc);
    void emitStrFromCStr(llvm::Value* dst, llvm::Value* cap, llvm::Value* cstr);
    void emitStrFromChar(llvm::Value* dst, llvm::Value* cap, llvm::Value* c);

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    llvm::IRBuilder<>& B;
    RuntimeFunctionCache& RtFns;
    std::function<llvm::StructType*(int64_t)> StrStructTypeOf;

    std::map<std::string, llvm::GlobalVariable*> StrGVs;
    // Interned { length, bytes } string structs, for constants whose value is
    // used where a string variable would be.
    std::map<std::string, llvm::GlobalVariable*> StructGVs;

    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
    llvm::IntegerType* i8Ty() const { return llvm::Type::getInt8Ty(Ctx); }
    llvm::PointerType* ptrTy() const { return llvm::PointerType::get(Ctx, 0); }
};
