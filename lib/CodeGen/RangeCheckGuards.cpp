#include "RangeCheckGuards.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"

bool RangeCheckGuards::rangeChecksAt(plang::SourceLocation Loc) const {
    return Opts.switchOn(plang::Switch::RangeChecks, Loc);
}

bool RangeCheckGuards::assertionsAt(plang::SourceLocation Loc) const {
    return Opts.switchOn(plang::Switch::Assertions, Loc);
}

bool RangeCheckGuards::boolEvalAt(plang::SourceLocation Loc) const {
    return Opts.switchOn(plang::Switch::BoolEval, Loc);
}

bool RangeCheckGuards::ioChecksAt(plang::SourceLocation Loc) const {
    return Opts.switchOn(plang::Switch::IOChecks, Loc);
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

void RangeCheckGuards::emitTpRunError(int64_t Code) {
    B.CreateCall(
        RtFns.getExternFnN("plang_tp_runerror", llvm::Type::getVoidTy(Ctx), {i64Ty()}),
        {llvm::ConstantInt::get(i64Ty(), static_cast<uint64_t>(Code), /*isSigned=*/true)});
}

void RangeCheckGuards::emitDivZeroCheck(llvm::Value* divisor, const char* op,
                                         unsigned Width) {
    // Without this the hardware raises SIGFPE, which surfaces as a bare
    // "Floating point exception" with no source context.
    auto* isZero = B.CreateICmpEQ(divisor,
        llvm::ConstantInt::get(intTy(Width), 0), "divzero");
    emitGuard(isZero, "divzero", [&] {
        // Borland/FPC's own numbered run-time error for division by zero
        // (confirmed against `fpc -Mtp`: `a div 0`/`a mod 0` both report
        // "Runtime error 200" and exit 200).
        if (isTurbo()) { emitTpRunError(200); return; }
        B.CreateCall(
            RtFns.getExternFnN("plang_err_div_zero", llvm::Type::getVoidTy(Ctx), {ptrTy()}),
            {Strings.internStrPtr(op)});
    });
}

void RangeCheckGuards::emitDivOverflowCheck(llvm::Value* dividend,
                                             llvm::Value* divisor,
                                             unsigned Width) {
    // -2^(Width-1) (minint at this width) has no positive counterpart
    // representable at the same width, so div overflows for this one
    // dividend/divisor pair despite the divisor being nonzero.  Unguarded,
    // SDiv here is signed-overflow UB: in practice either a hardware SIGFPE
    // (x86 idiv traps on overflow, same as it does on a zero divisor) or a
    // value the optimizer is free to fold away, which is exactly the
    // SIGFPE-vs-wrong-answer split this guard closes.
    auto* isMinInt = B.CreateICmpEQ(dividend,
        llvm::ConstantInt::get(intTy(Width), llvm::APInt::getSignedMinValue(Width)),
        "div.ismin");
    auto* isNegOne = B.CreateICmpEQ(divisor,
        llvm::ConstantInt::getSigned(intTy(Width), -1), "div.isnegone");
    auto* bad = B.CreateAnd(isMinInt, isNegOne, "div.overflow");
    emitGuard(bad, "divoverflow", [&] {
        // Borland/FPC's "Runtime error 215: Arithmetic overflow error".
        if (isTurbo()) { emitTpRunError(215); return; }
        B.CreateCall(
            RtFns.getExternFnN("plang_err_div_overflow",
                         llvm::Type::getVoidTy(Ctx), {}),
            {});
    });
}

void RangeCheckGuards::emitModDivisorCheck(llvm::Value* divisor,
                                            unsigned Width) {
    // ISO §6.7.2.2 defines mod only for a positive divisor; Turbo's mod has
    // no such restriction -- it takes its sign from the DIVIDEND instead
    // (plain srem, the same computation CGBinaryOps' Mod case already falls
    // back to for Turbo, skipping the ISO "0 <= mod < divisor" adjustment
    // right below this guard).  Confirmed against `fpc -Mtp`: `7 mod (-3)`
    // is 1, `(-7) mod (-3)` is -1, neither of which ISO's rule would even
    // allow evaluating (a negative divisor is a dynamic-violation there).
    // So for Turbo this is not just re-routed to a different reporter, as
    // every other guard in this file is -- a NEGATIVE divisor must not be
    // rejected at all.  A ZERO divisor is a different matter: `srem` by
    // zero is undefined behaviour hardware traps on exactly the way SDiv
    // does (emitDivZeroCheck's own reason for existing), and Turbo does
    // NOT let this one through -- confirmed against `fpc -Mtp`: `a mod 0`
    // reports "Runtime error 200", the SAME number as `a div 0`, not a
    // silent crash.  So Turbo still checks here, just a strictly narrower
    // condition (== 0, not <= 0) than ISO's.
    if (isTurbo()) {
        auto* isZero = B.CreateICmpEQ(divisor,
            llvm::ConstantInt::get(intTy(Width), 0), "mod.divzero");
        emitGuard(isZero, "mod.divzero", [&] { emitTpRunError(200); });
        return;
    }
    auto* bad = B.CreateICmpSLE(divisor,
        llvm::ConstantInt::get(intTy(Width), 0), "mod.baddiv");
    emitGuard(bad, "mod.baddiv", [&] {
        B.CreateCall(
            RtFns.getExternFnN("plang_err_mod_divisor", llvm::Type::getVoidTy(Ctx),
                         {i64Ty()}),
            {ToI64(divisor)});
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
        // Borland/FPC's "Runtime error 216: General protection fault" --
        // what a real Turbo/FPC program gets from the OS trapping a bad
        // pointer access, including a nil dereference (confirmed against
        // `fpc -Mtp`: `p := nil; writeln(p^);` reports 216).  plang checks
        // explicitly rather than relying on a trap, but reports the same
        // number so a program's exit status still means what it would on
        // real Turbo/FPC.
        if (isTurbo()) { emitTpRunError(216); return; }
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
        // Borland/FPC's "Runtime error 201: Range check error" covers BOTH
        // shapes this guard serves -- an array/string index out of bounds
        // and a value out of a subrange's bounds alike (confirmed against
        // `fpc -Mtp`: an out-of-range subrange assignment with no indexing
        // involved at all still reports 201, the same code an out-of-bounds
        // array access does).
        if (isTurbo()) { emitTpRunError(201); return; }
        B.CreateCall(
            RtFns.getExternFnN(isIndex ? "plang_err_index" : "plang_err_range",
                         llvm::Type::getVoidTy(Ctx), {i64Ty(), i64Ty(), i64Ty()}),
            {v, loV, hiV});
    });
}
