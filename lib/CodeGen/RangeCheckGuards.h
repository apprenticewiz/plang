// RangeCheckGuards.h — ISO runtime-check guards/traps.
#pragma once

#include <functional>

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceLocation.h"

#include "RuntimeFunctionCache.h"
#include "StringRuntime.h"

class RangeCheckGuards {
public:
    /// CurFn binds to the storage location of whatever "the function
    /// currently being emitted" is (Codegen::Impl::curFunc today), not a
    /// copy taken once -- emitGuard needs whichever function is live when it
    /// runs, and that changes every function activation.
    RangeCheckGuards(llvm::LLVMContext& Ctx, llvm::IRBuilder<>& B,
                      llvm::Function*& CurFn,
                      RuntimeFunctionCache& RtFns, StringRuntime& Strings,
                      const plang::LangOptions& Opts, bool& NilChecks,
                      std::function<llvm::Value*(llvm::Value*)> ToI64)
        : Ctx(Ctx), B(B), CurFn(CurFn), RtFns(RtFns), Strings(Strings),
          Opts(Opts), NilChecks(NilChecks), ToI64(std::move(ToI64)) {}

    /// Whether range checking is on where \p Loc is.
    [[nodiscard]] bool rangeChecksAt(plang::SourceLocation Loc) const;
    void emitGuard(llvm::Value* failCond, const char* name,
                   llvm::function_ref<void()> emitFail);
    void emitDivZeroCheck(llvm::Value* divisor, const char* op);
    /// ISO §6.7.2.2 mod is defined only for a positive divisor; folds the
    /// negatives together and replaces emitDivZeroCheck for that operator.
    void emitModDivisorCheck(llvm::Value* divisor);
    void emitNilCheck(llvm::Value* ptr);
    void emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi, bool isIndex,
                         plang::SourceLocation Loc);
    /// emitRangeCheck for bounds that are only known at run time.
    void emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo, llvm::Value* hi,
                            bool isIndex, plang::SourceLocation Loc);

private:
    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    RuntimeFunctionCache& RtFns;
    StringRuntime& Strings;
    const plang::LangOptions& Opts;
    bool& NilChecks;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
    llvm::PointerType* ptrTy() const { return llvm::PointerType::get(Ctx, 0); }
};
