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
    /// Whether TP's Assertions switch is on where \p Loc is -- the same
    /// position-keyed Opts.switchOn query as rangeChecksAt, just a different
    /// Switch.  CGProcCall::emitCallStmt's Assert arm is the one call site: a
    /// program's `{$C-}` has to make Assert(cond) compile to nothing at all,
    /// not even evaluating cond, so the decision has to be made before
    /// anything about the call is emitted -- unlike every guard below, which
    /// always evaluates its operands and only branches around the failure.
    [[nodiscard]] bool assertionsAt(plang::SourceLocation Loc) const;
    /// Whether the active dialect is Turbo -- read both internally (every
    /// guard below routes its failure through the plang_tp_* reporter
    /// family instead of the shared ISO/EP plang_err_* one when this is
    /// true) and by CGBinaryOps' own mod codegen, which has no LangOptions
    /// of its own to ask: TP's mod takes its sign from the dividend (plain
    /// srem, confirmed against `fpc -Mtp`), not ISO §6.7.2.2's "0 <= mod <
    /// divisor" normalization, so that adjustment has to be skipped there
    /// too, not just the divisor-positive guard here.
    [[nodiscard]] bool isTurbo() const { return Opts.turbo(); }
    void emitGuard(llvm::Value* failCond, const char* name,
                   llvm::function_ref<void()> emitFail);
    /// \p Width is the operand's Type::Width (ISO 7185 and Extended Pascal
    /// always pass 64, the only width either dialect's Integer has, so the
    /// default reproduces today's behavior for both exactly).  It has to
    /// match \p divisor's own LLVM type: the guard compares divisor directly
    /// against a same-typed zero rather than normalizing through ToI64
    /// first, so a caller that ever hands this a genuinely-16-bit divisor
    /// without also passing Width=16 gets an LLVM operand-type-mismatch
    /// crash building the icmp, not a silently wrong check.
    void emitDivZeroCheck(llvm::Value* divisor, const char* op,
                           unsigned Width = 64);
    /// minint div -1 is the one div with a nonzero divisor that still has no
    /// representable result (2^Width-1 does not fit a positive int of that
    /// width) -- a sibling to emitDivZeroCheck, not a replacement for it,
    /// since the two guard different divisor values.  Not needed for mod:
    /// emitModDivisorCheck already rejects every negative divisor, -1
    /// included.
    ///
    /// \p Width must match minint for the WIDTH THE DIVISION IS ACTUALLY
    /// PERFORMED AT, not just dividend/divisor's LLVM type -- comparing
    /// against a fixed 64-bit minint (as this used to, unconditionally) means
    /// a 16-bit Integer's true overflow pair (INT16_MIN div -1) is never
    /// recognized once it is sign- or zero-extended to a wider carrier type,
    /// which is a silently-wrong-answer bug, not a crash: the guard simply
    /// never fires and SDiv computes whatever the wider type's arithmetic
    /// gives.
    void emitDivOverflowCheck(llvm::Value* dividend, llvm::Value* divisor,
                               unsigned Width = 64);
    /// ISO §6.7.2.2 mod is defined only for a positive divisor; folds the
    /// negatives together and replaces emitDivZeroCheck for that operator.
    /// Same \p Width contract as emitDivZeroCheck.  The diagnostic call's
    /// own ABI is fixed at i64 regardless of Width (one reporter signature
    /// serves every width), so \p divisor is widened through ToI64 -- same
    /// as every other diagnostic value below -- only for that call, not for
    /// the guard's own comparison.
    void emitModDivisorCheck(llvm::Value* divisor, unsigned Width = 64);
    void emitNilCheck(llvm::Value* ptr);
    /// Unlike the div/mod guards above, this one does not need a Width
    /// parameter: lo and hi arrive as exact int64_t bounds (from
    /// ordinalRange, SubLo/SubHi, etc. -- already correct for whatever width
    /// the type actually is, since those fields store the absolute bound,
    /// not a Width-bit pattern), and emitRangeCheckDyn normalizes val to i64
    /// through the shared ToI64 before comparing regardless of val's
    /// original LLVM type.  Building lo/hi at a narrower IntegerType here
    /// instead would not fix anything -- it would round-trip a negative
    /// bound through ToI64's unconditional zero-extend and corrupt it (e.g.
    /// -32768 at i16 becomes +32768 once zero-extended to i64) -- so this
    /// stays i64Ty()-only on purpose.
    void emitRangeCheck(llvm::Value* val, int64_t lo, int64_t hi, bool isIndex,
                         plang::SourceLocation Loc);
    /// emitRangeCheck for bounds that are only known at run time.
    void emitRangeCheckDyn(llvm::Value* val, llvm::Value* lo, llvm::Value* hi,
                            bool isIndex, plang::SourceLocation Loc);

private:
    /// Emits the call every Turbo-routed guard failure below shares: TP's
    /// parallel plang_tp_runerror(code) reporter (runtime/plang_sys.cpp),
    /// which prints "Runtime error <code> at $<addr>" and exits with status
    /// \p Code itself -- never the shared ISO/EP PlangRuntimeErrorStatus.
    /// Factored out because five call sites (div-zero, div-overflow, nil,
    /// and both range-check shapes) would otherwise repeat the same
    /// getExternFnN/ConstantInt boilerplate for the five numbered codes
    /// Borland/FPC assign each check (200/215/201/216).
    void emitTpRunError(int64_t Code);

    llvm::LLVMContext& Ctx;
    llvm::IRBuilder<>& B;
    llvm::Function*& CurFn;
    RuntimeFunctionCache& RtFns;
    StringRuntime& Strings;
    const plang::LangOptions& Opts;
    bool& NilChecks;
    std::function<llvm::Value*(llvm::Value*)> ToI64;
    llvm::IntegerType* i64Ty() const { return llvm::Type::getInt64Ty(Ctx); }
    /// The operand-matching type for a Width-parameterized guard -- see
    /// emitDivZeroCheck's comment.  intTy(64) and i64Ty() are the same LLVM
    /// type object (a context interns integer types by bit width), so every
    /// call site that still passes the Width=64 default gets IR identical to
    /// what a fixed i64Ty() produced before this existed.
    llvm::IntegerType* intTy(unsigned Width) const { return llvm::Type::getIntNTy(Ctx, Width); }
    llvm::PointerType* ptrTy() const { return llvm::PointerType::get(Ctx, 0); }
};
