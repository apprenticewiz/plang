// RuntimeFunctionCache.h — the extern-fn-decl cache for runtime entry points.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

class RuntimeFunctionCache {
public:
    RuntimeFunctionCache(llvm::LLVMContext& Ctx, llvm::Module& Mod)
        : Ctx(Ctx), Mod(Mod) {}

    /// Declares (once) an external function \p name of type \p ty, or returns
    /// the previously-cached declaration if one for this name already exists.
    /// Keyed by name only: a second request under an already-cached name
    /// returns the function built for the *first* requested signature.
    llvm::Function* getExternFn(const std::string& name, llvm::FunctionType* ty);
    llvm::Function* getExternFnN(const std::string& name, llvm::Type* retTy,
                                  std::vector<llvm::Type*> params);
    llvm::Function* getRTMathRR(const std::string& name); // double(double)
    llvm::Function* getRTMathRI(const std::string& name); // i64(double)
    llvm::Function* getRTMathII(const std::string& name); // i64(i64)
    llvm::Function* getRuntimeFn(const std::string& name, llvm::Type* argTy);
    llvm::Function* getRuntimeBoolFn(const std::string& name);
    llvm::Function* getRuntimeNewFn();     // ptr plang_new(i64)
    llvm::Function* getRuntimeDisposeFn(); // void plang_dispose(ptr)
    llvm::Function* getRuntimeHaltFn();    // void plang_halt(i64) noreturn
    // The value-conformant-array-copy shadow stack (runtime/plang_sys.cpp)
    // that lets a non-local goto's landing pad find and free copies made by
    // activations it skipped straight past -- see ConfArrStack's own comment
    // there.
    llvm::Function* getConfArrMarkFn();    // i64 plang_confarr_mark(void)
    llvm::Function* getConfArrPushFn();    // void plang_confarr_push(ptr)
    llvm::Function* getConfArrUnwindFn();  // void plang_confarr_unwind(i64)

private:
    llvm::LLVMContext& Ctx;
    llvm::Module& Mod;
    std::map<std::string, llvm::Function*> Fns;
};
