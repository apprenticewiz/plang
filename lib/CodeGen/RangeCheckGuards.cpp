#include "RangeCheckGuards.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"

bool RangeCheckGuards::rangeChecksAt(plang::SourceLocation Loc) const {
    return Opts.switchOn(plang::Switch::RangeChecks, Loc);
}

void RangeCheckGuards::emitGuard(llvm::Value* failCond, const char* name,
                                  llvm::function_ref<void()> emitFail) {
    auto* failBB = llvm::BasicBlock::Create(Ctx, llvm::Twine(name) + ".fail", CurFn);
    auto* contBB = llvm::BasicBlock::Create(Ctx, llvm::Twine(name) + ".ok",   CurFn);
    B.CreateCondBr(failCond, failBB, contBB);
    B.SetInsertPoint(failBB);
    emitFail();
    B.CreateUnreachable(); // the reporter is [[noreturn]]
    B.SetInsertPoint(contBB);
}

void RangeCheckGuards::emitDivZeroCheck(llvm::Value* divisor, const char* op) {
    // Without this the hardware raises SIGFPE, which surfaces as a bare
    // "Floating point exception" with no source context.
    auto* isZero = B.CreateICmpEQ(divisor,
        llvm::ConstantInt::get(i64Ty(), 0), "divzero");
    emitGuard(isZero, "divzero", [&] {
        B.CreateCall(
            RtFns.getExternFnN("plang_err_div_zero", llvm::Type::getVoidTy(Ctx), {ptrTy()}),
            {Strings.internStrPtr(op)});
    });
}

void RangeCheckGuards::emitModDivisorCheck(llvm::Value* divisor) {
    // ISO §6.7.2.2 defines mod only for a positive divisor, so this subsumes
    // the div-by-zero test rather than sitting alongside it.
    auto* bad = B.CreateICmpSLE(divisor,
        llvm::ConstantInt::get(i64Ty(), 0), "mod.baddiv");
    emitGuard(bad, "mod.baddiv", [&] {
        B.CreateCall(
            RtFns.getExternFnN("plang_err_mod_divisor", llvm::Type::getVoidTy(Ctx),
                         {i64Ty()}),
            {divisor});
    });
}

void RangeCheckGuards::emitNilCheck(llvm::Value* ptr) {
    // ISO §6.5.4: dereferencing nil is an error.  Left to the hardware it is a
    // segmentation fault with no indication of which line, which is the least
    // useful thing a Pascal implementation can say.  It has its own flag:
    // this was grouped with the range checks until 0.1.2, so -fno-range-checks
    // silently removed it, and a program compiled that way answered a nil
    // dereference with a signal rather than a diagnostic.  A single compare
    // against null is also not what anyone turns range checking off to avoid.
    if (!NilChecks || !ptr) return;
    auto* isNil = B.CreateICmpEQ(
        ptr, llvm::ConstantPointerNull::get(ptrTy()), "isnil");
    emitGuard(isNil, "nilderef", [&] {
        B.CreateCall(
            RtFns.getExternFnN("plang_err_nil_deref", llvm::Type::getVoidTy(Ctx), {}),
            {});
    });
}

void RangeCheckGuards::emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi,
                                       bool isIndex, plang::SourceLocation Loc) {
    if (!rangeChecksAt(Loc)) return;
    emitRangeCheckDyn(val, llvm::ConstantInt::get(i64Ty(), lo, true),
                      llvm::ConstantInt::get(i64Ty(), hi, true), isIndex, Loc);
}

void RangeCheckGuards::emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo,
                                          llvm::Value* hi, bool isIndex,
                                          plang::SourceLocation Loc) {
    if (!rangeChecksAt(Loc)) return;
    auto* v      = ToI64(val);
    auto* loV    = ToI64(lo);
    auto* hiV    = ToI64(hi);
    auto* tooLow = B.CreateICmpSLT(v, loV, "rng.lo");
    auto* tooHi  = B.CreateICmpSGT(v, hiV, "rng.hi");
    auto* bad    = B.CreateOr(tooLow, tooHi, "rng.bad");
    emitGuard(bad, isIndex ? "bounds" : "range", [&] {
        B.CreateCall(
            RtFns.getExternFnN(isIndex ? "plang_err_index" : "plang_err_range",
                         llvm::Type::getVoidTy(Ctx), {i64Ty(), i64Ty(), i64Ty()}),
            {v, loV, hiV});
    });
}
