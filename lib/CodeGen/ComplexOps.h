// ComplexOps.h — EP §6.4.2.2 complex arithmetic.
#pragma once

#include <functional>
#include <string>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"

#include "RuntimeFunctionCache.h"

class ComplexOps {
public:
    /// ToDouble/EntryAlloca are narrow closures into methods that belong to
    /// a different, wider cluster (scalar coercion / alloca placement) --
    /// not duplicated here, just reached through a named seam.
    ComplexOps(llvm::LLVMContext& Ctx, llvm::Type* DblTy, llvm::PointerType* PtrTy,
               llvm::IRBuilder<>& B, RuntimeFunctionCache& RtFns,
               std::function<llvm::Value*(llvm::Value*)> ToDouble,
               std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> EntryAlloca)
        : Ctx(Ctx), DblTy(DblTy), PtrTy(PtrTy), B(B), RtFns(RtFns),
          ToDouble(std::move(ToDouble)), EntryAlloca(std::move(EntryAlloca)) {}

    /// The LLVM struct type for EP complex: { double, double }.
    llvm::StructType* complexTy();
    /// Build a { double, double } aggregate from two double values.
    llvm::Value* makeComplex(llvm::Value* re, llvm::Value* im);
    /// Coerce a scalar or complex value to a { double, double } complex
    /// aggregate.  If the value is already complexTy, it is returned as-is.
    /// Integer values are first widened to double.
    llvm::Value* coerceToComplex(llvm::Value* v);

    llvm::Value* emitComplexAdd(llvm::Value* a, llvm::Value* b);
    llvm::Value* emitComplexSub(llvm::Value* a, llvm::Value* b);
    llvm::Value* emitComplexMul(llvm::Value* a, llvm::Value* b);
    llvm::Value* emitComplexDiv(llvm::Value* a, llvm::Value* b);
    /// Complex power via runtime plang_cpow_out.
    llvm::Value* emitComplexPow(llvm::Value* a, llvm::Value* b);
    /// Call a (re_out, im_out, re_in, im_in) runtime function and return complex.
    llvm::Value* callComplexUnary(const std::string& name, llvm::Value* z);

private:
    llvm::LLVMContext& Ctx;
    llvm::Type* DblTy;
    llvm::PointerType* PtrTy;
    llvm::IRBuilder<>& B;
    RuntimeFunctionCache& RtFns;
    std::function<llvm::Value*(llvm::Value*)> ToDouble;
    std::function<llvm::AllocaInst*(llvm::Type*, const std::string&)> EntryAlloca;
    llvm::StructType* Ty{nullptr};
};
