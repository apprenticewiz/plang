/// checked_arith_test.cpp -- checked arithmetic and range-check guards
/// consulting the operand's actual width, rather than assuming 64
///
/// ISO 7185 and Extended Pascal have one Integer type, always 64 bits wide
/// (Type::Width, LangOptions::defaultIntWidth()).  Turbo's Integer is
/// deliberately 16-bit (see LangOptions::defaultIntWidth()'s own comment and
/// storage_test.cpp's TheDialectDecidesHowWideAnIntegerIs/
/// MaxintIsTheLargestValueTheDialectsIntegerHolds, which already prove the
/// width itself is threaded correctly), and Byte/ShortInt/Word/LongInt add
/// four more widths beside it.  Three places still hardcoded 64 as though it
/// were the only width any dialect could have: RangeCheckGuards' div/mod
/// guards (lib/CodeGen/RangeCheckGuards.{h,cpp}), the checked-arithmetic
/// helpers constant folders share with codegen (include/plang/Basic/
/// Arith.h), and Type.h's ordinalRange, which answered nullopt ("no bounded
/// range") for every Integer instead of only the 64-bit one.  This file is
/// this task's own version of storage_test.cpp's "deliberately deferred"
/// cases: -std=turbo is not yet reachable from the command line (Dialects.def
/// still marks it Implemented=false), so nothing here has a lit-testable,
/// CLI-observable proxy today.  Every case instead constructs the width it
/// wants directly -- a LangOptions{Std=Turbo}-shaped fact for ordinalRange
/// (via TypeContext::getInt, the same factory storage_test.cpp's own
/// TwoIntegersOfOneWidthAreOneType and AnIntegerIsAsWideAsItWasAskedFor
/// already use) or an explicit Width argument for the checked-arithmetic
/// helpers and range-check guards, which take one for exactly this reason.
/// Revisit once Turbo's own div/mod codegen exists and actually calls these
/// with a real 16-bit operand, at which point the width argument threaded
/// here can be driven from a real compile instead of asserted by hand.
///
/// Grouped in one file (rather than splitting the Type.h piece into
/// sema_test.cpp, which is scoped to the Builtins.def X-macro cases and says
/// so in its own header) because the three pieces are one feature -- checked
/// arithmetic has to agree with the type it is checking against, wherever
/// that type's width is asked -- landing in one PR, not three unrelated
/// storage questions.  Lives under test/unittests/CodeGen/, not Sema/,
/// because two of the three pieces (RangeCheckGuards and Arith.h, which its
/// own header comment says is "shared by the constant folders and by
/// codegen") need CodeGen's internal LLVM scaffolding and its extra
/// LLVM-linking CMake stanza; the third (ordinalRange) does not, but splitting
/// it out over a build-system distinction nobody reading the feature would
/// care about seemed worse than one extra #include here.

#include "RangeCheckGuards.h"
#include "RuntimeFunctionCache.h"
#include "StringRuntime.h"

#include "plang/AST/TypeContext.h"
#include "plang/Basic/Arith.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Sema/Type.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <utility>

using namespace plang;

// ---------------------------------------------------------------------------
// RangeCheckGuards: emitDivZeroCheck/emitDivOverflowCheck/emitModDivisorCheck
//
// Each guard compares an LLVM value directly against a same-typed constant
// with no normalizing widen step first (unlike emitRangeCheck/
// emitRangeCheckDyn, which route every operand through the shared ToI64 and
// so need no Width parameter of their own -- see RangeCheckGuards.h's
// comment on emitRangeCheck for why that one was deliberately left alone).
// That means the LLVM type of the comparison constant has to match Width, or
// building the icmp itself is an LLVM operand-type-mismatch crash the moment
// a real (non-i64) operand reaches it -- exactly the crash class a caller
// with a genuinely-16-bit dividend/divisor would hit today without the fix
// below.
// ---------------------------------------------------------------------------

namespace {

/// A two-argument void(WidthTy, WidthTy) function, positioned at its entry
/// block and wired up with a RangeCheckGuards ready to emit into it -- the
/// same construction CodeGenTypes.cpp does for the real one (see its own
/// comment on why RangeCheckGuards binds curFunc/nilChecks by reference),
/// just without a whole Codegen::Impl around it.
struct GuardHarness {
    llvm::LLVMContext Ctx;
    llvm::Module      Mod;
    llvm::IRBuilder<> B;
    llvm::Function*   Fn;
    RuntimeFunctionCache RtFns;
    StringRuntime        Strings;
    LangOptions           Opts;
    bool                  NilChecks;
    RangeCheckGuards      Guards;

    explicit GuardHarness(unsigned Width)
        : Mod("checked_arith_test", Ctx), B(Ctx),
          Fn(llvm::Function::Create(
              llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx),
                  {llvm::Type::getIntNTy(Ctx, Width), llvm::Type::getIntNTy(Ctx, Width)},
                  /*isVarArg=*/false),
              llvm::GlobalValue::ExternalLinkage, "f", Mod)),
          RtFns(Ctx, Mod),
          // Never invoked by the div/mod guards under test: StringRuntime::
          // internStrPtr (the one emitDivZeroCheck calls) never reaches
          // StrStructTypeOf -- only internStrStruct does.
          Strings(Ctx, Mod, B, RtFns, [](int64_t) -> llvm::StructType* { return nullptr; }),
          NilChecks(true),
          Guards(Ctx, B, Fn, RtFns, Strings, Opts, NilChecks,
                 [this](llvm::Value* V) { return toI64(V); }) {
        B.SetInsertPoint(llvm::BasicBlock::Create(Ctx, "entry", Fn));
    }

    llvm::Value* arg(unsigned I) { return Fn->getArg(I); }

    /// Stand-in for Codegen::Impl::toI64's integer path (the double path is
    /// never exercised here): zero-extends anything narrower than i64,
    /// otherwise a no-op.  Only emitModDivisorCheck's diagnostic-call
    /// argument goes through this below.
    llvm::Value* toI64(llvm::Value* V) {
        if (V->getType()->isIntegerTy(64)) return V;
        return B.CreateZExt(V, llvm::Type::getInt64Ty(Ctx), "to.i64");
    }

    /// The one instruction named \p Name -- every comparison this file
    /// checks is a uniquely-named icmp (emitDivZeroCheck's "divzero",
    /// emitDivOverflowCheck's "div.ismin"/"div.isnegone",
    /// emitModDivisorCheck's "mod.baddiv"), each built exactly once per
    /// harness/test.
    llvm::CmpInst* namedCmp(const char* Name) {
        for (auto& BB : *Fn)
            for (auto& I : BB)
                if (I.getName() == Name) return llvm::dyn_cast<llvm::CmpInst>(&I);
        return nullptr;
    }

    /// The call to runtime function \p Name, or null if the guard never
    /// reached its failure path (it always does here -- emitGuard emits the
    /// call unconditionally into the fail block, it is just never branched
    /// to at run time by anything this file executes).
    llvm::CallInst* callTo(const char* Name) {
        for (auto& BB : *Fn)
            for (auto& I : BB)
                if (auto* C = llvm::dyn_cast<llvm::CallInst>(&I))
                    if (C->getCalledFunction() && C->getCalledFunction()->getName() == Name)
                        return C;
        return nullptr;
    }
};

} // namespace

TEST(RangeCheckGuards, DivZeroCheckComparesAgainstAZeroOfTheOperandsOwnWidth) {
    GuardHarness H(16);
    H.Guards.emitDivZeroCheck(H.arg(1), "div", 16);
    auto* Cmp = H.namedCmp("divzero");
    ASSERT_NE(Cmp, nullptr);
    auto* Rhs = llvm::dyn_cast<llvm::ConstantInt>(Cmp->getOperand(1));
    ASSERT_NE(Rhs, nullptr);
    EXPECT_EQ(Rhs->getType()->getIntegerBitWidth(), 16u);
    EXPECT_EQ(Rhs->getSExtValue(), 0);
}

TEST(RangeCheckGuards, DivZeroCheckDefaultsToI64ForIso7185AndEp) {
    // No Width argument -- the shape every real call site (all of them ISO
    // 7185/EP today) still uses, so this is what proves the default is
    // exactly today's behavior rather than a new one that happens to agree
    // sometimes.
    GuardHarness H(64);
    H.Guards.emitDivZeroCheck(H.arg(1), "div");
    auto* Cmp = H.namedCmp("divzero");
    ASSERT_NE(Cmp, nullptr);
    EXPECT_EQ(Cmp->getOperand(1)->getType()->getIntegerBitWidth(), 64u);
}

TEST(RangeCheckGuards, DivOverflowGuardCatchesTurboIntegerMinintDivNegativeOne) {
    // The live bug this task exists to close: INT16_MIN div -1 overflows a
    // 16-bit Integer exactly the way INT64_MIN div -1 overflows a 64-bit
    // one, but a guard that always compared against 64-bit minint would
    // never recognize -32768 as it, and SDiv would compute whatever a
    // 16-bit-carried, non-overflow-checked division gives -- a silently
    // wrong answer, not a crash.
    GuardHarness H(16);
    H.Guards.emitDivOverflowCheck(H.arg(0), H.arg(1), 16);

    auto* IsMin = H.namedCmp("div.ismin");
    ASSERT_NE(IsMin, nullptr);
    auto* MinC = llvm::dyn_cast<llvm::ConstantInt>(IsMin->getOperand(1));
    ASSERT_NE(MinC, nullptr);
    EXPECT_EQ(MinC->getType()->getIntegerBitWidth(), 16u);
    EXPECT_EQ(MinC->getSExtValue(), INT16_MIN); // not INT64_MIN

    auto* IsNegOne = H.namedCmp("div.isnegone");
    ASSERT_NE(IsNegOne, nullptr);
    auto* NegOneC = llvm::dyn_cast<llvm::ConstantInt>(IsNegOne->getOperand(1));
    ASSERT_NE(NegOneC, nullptr);
    EXPECT_EQ(NegOneC->getType()->getIntegerBitWidth(), 16u);
    EXPECT_EQ(NegOneC->getSExtValue(), -1);
}

TEST(RangeCheckGuards, DivOverflowGuardDefaultsToInt64MinintForIso7185AndEp) {
    GuardHarness H(64);
    H.Guards.emitDivOverflowCheck(H.arg(0), H.arg(1)); // default Width=64
    auto* IsMin = H.namedCmp("div.ismin");
    ASSERT_NE(IsMin, nullptr);
    auto* MinC = llvm::dyn_cast<llvm::ConstantInt>(IsMin->getOperand(1));
    ASSERT_NE(MinC, nullptr);
    EXPECT_EQ(MinC->getType()->getIntegerBitWidth(), 64u);
    EXPECT_EQ(MinC->getSExtValue(), INT64_MIN);
}

TEST(RangeCheckGuards, ModDivisorCheckComparesAtWidthButWidensOnlyForTheFixedAbiDiagnostic) {
    GuardHarness H(16);
    H.Guards.emitModDivisorCheck(H.arg(1), 16);

    auto* Cmp = H.namedCmp("mod.baddiv");
    ASSERT_NE(Cmp, nullptr);
    EXPECT_EQ(Cmp->getOperand(1)->getType()->getIntegerBitWidth(), 16u);

    // plang_err_mod_divisor's signature is fixed at i64 regardless of Width
    // (one reporter serves every width) -- the 16-bit divisor has to widen
    // to reach it.
    auto* Call = H.callTo("plang_err_mod_divisor");
    ASSERT_NE(Call, nullptr);
    ASSERT_EQ(Call->arg_size(), 1u);
    EXPECT_EQ(Call->getArgOperand(0)->getType()->getIntegerBitWidth(), 64u);
}

TEST(RangeCheckGuards, ModDivisorCheckDefaultsToI64ForIso7185AndEp) {
    GuardHarness H(64);
    H.Guards.emitModDivisorCheck(H.arg(1));
    auto* Cmp = H.namedCmp("mod.baddiv");
    ASSERT_NE(Cmp, nullptr);
    EXPECT_EQ(Cmp->getOperand(1)->getType()->getIntegerBitWidth(), 64u);
}

// ---------------------------------------------------------------------------
// Arith.h: checkedAdd/checkedSub/checkedMul/checkedNeg/isoPow
//
// Every case below without a Width/Signed argument exercises exactly the
// call shape SemaType.cpp's constBound and CodeGen/ConstFold.cpp already
// use today (both ISO 7185 and Extended Pascal, neither of which this task
// touches) -- proving the new parameters are additive, not a behavior change
// for either dialect, is the same int64_t-overflow-only answer these gave
// before Width/Signed existed.
// ---------------------------------------------------------------------------

TEST(Arith, CheckedAddIsUnaffectedAtTheDefaultWidth) {
    EXPECT_EQ(checkedAdd(30000, 30000), 60000);
    EXPECT_EQ(checkedAdd(INT64_MAX, 1), std::nullopt);
}

TEST(Arith, CheckedAddOverflowsAtANarrowerWidthEvenWhenInt64DoesNot) {
    // 60000 does not overflow int64_t, but it does overflow Turbo's 16-bit
    // signed Integer (max 32767) -- silently wrong overflow semantics is
    // exactly the failure mode a constant folder must not have.
    EXPECT_EQ(checkedAdd(30000, 30000, 16, true), std::nullopt);
    EXPECT_EQ(checkedAdd(100, 27, 16, true), 127);
    EXPECT_EQ(checkedAdd(32760, 7, 16, true), 32767);
    EXPECT_EQ(checkedAdd(32760, 8, 16, true), std::nullopt);
}

TEST(Arith, CheckedAddRespectsAnUnsignedNarrowRange) {
    // Turbo's Byte: 0..255.
    EXPECT_EQ(checkedAdd(200, 55, 8, false), 255);
    EXPECT_EQ(checkedAdd(200, 56, 8, false), std::nullopt);
    EXPECT_EQ(checkedAdd(200, -201, 8, false), std::nullopt); // negative result
}

TEST(Arith, CheckedSubUnderflowsAtANarrowerWidth) {
    EXPECT_EQ(checkedSub(-32767, 1, 16, true), -32768);
    EXPECT_EQ(checkedSub(-32768, 1, 16, true), std::nullopt);
}

TEST(Arith, CheckedMulOverflowsAtANarrowerWidth) {
    EXPECT_EQ(checkedMul(100, 300, 16, true), 30000);
    EXPECT_EQ(checkedMul(200, 200, 16, true), std::nullopt); // 40000 > 32767
}

TEST(Arith, CheckedNegHasNoRepresentableResultForMinintAtAnyWidth) {
    EXPECT_EQ(checkedNeg(INT64_MIN), std::nullopt);        // 64-bit minint (default)
    EXPECT_EQ(checkedNeg(-32768, 16, true), std::nullopt);  // Turbo Integer's minint
    EXPECT_EQ(checkedNeg(-32767, 16, true), 32767);
}

TEST(Arith, IsoPowOverflowsAtANarrowerWidth) {
    EXPECT_EQ(isoPow(2, 20), 1048576);                  // fits int64_t (default Width)
    EXPECT_EQ(isoPow(2, 14, 16, true), 16384);           // fits Turbo Integer
    EXPECT_EQ(isoPow(2, 15, 16, true), std::nullopt);    // 32768 > 32767
}

// ---------------------------------------------------------------------------
// Type.h: ordinalRange for TypeKind::Integer
//
// ISO 7185/EP's Integer (Width == 64) has to keep answering nullopt exactly
// as before -- an array over the whole of it still cannot be laid out -- so
// that case is asserted first and separately from the narrower ones.
// ---------------------------------------------------------------------------

TEST(OrdinalRange, ThePredefined64BitIntegerStaysUnbounded) {
    TypeContext C;
    EXPECT_EQ(ordinalRange(*C.getInteger()),      std::nullopt);
    EXPECT_EQ(ordinalRange(*C.getInt(64, true)),  std::nullopt);
    EXPECT_EQ(ordinalRange(*C.getInt(64, false)), std::nullopt);
}

TEST(OrdinalRange, ATurboWidthIntegerAnswersItsOwnBounds) {
    TypeContext C;
    EXPECT_EQ(ordinalRange(*C.getInt(16, true)),
              (std::pair<int64_t, int64_t>{-32768, 32767}));       // Integer
    EXPECT_EQ(ordinalRange(*C.getInt(8, false)),
              (std::pair<int64_t, int64_t>{0, 255}));              // Byte
    EXPECT_EQ(ordinalRange(*C.getInt(8, true)),
              (std::pair<int64_t, int64_t>{-128, 127}));           // ShortInt
    EXPECT_EQ(ordinalRange(*C.getInt(16, false)),
              (std::pair<int64_t, int64_t>{0, 65535}));            // Word
    EXPECT_EQ(ordinalRange(*C.getInt(32, true)),
              (std::pair<int64_t, int64_t>{-2147483648LL, 2147483647LL})); // LongInt
}
