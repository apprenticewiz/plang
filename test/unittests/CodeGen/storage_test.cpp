/// storage_test.cpp -- how wide a type is, and where its parts sit
///
/// Sema works sizes out without a DataLayout, because Turbo writes
/// `const BufSize = 4 * SizeOf(Integer)` and a constant has to fold long before
/// there is one.  That makes two answers to one question, which is the
/// arrangement that goes wrong quietly: a SizeOf that disagrees with the
/// layout sizes a GetMem or a BlockRead buffer wrong, and nothing says so
/// until the memory past the end of it is read back.
///
/// The cases that compiled a real program and just checked it didn't ICE
/// (the size-agreement assertion between Sema::byteSizeOf and
/// DataLayout::getTypeAllocSize aborts the process on disagreement, so
/// merely finishing is the whole test) migrated to test/CodeGen/ (issue
/// #43, Phase F) -- 7 became new lit files, 1 was folded next to an existing
/// VariantRecord case that was missing its exact wide-sibling ingredient,
/// and 4 turned out to already be covered, more thoroughly (by round-tripped
/// runtime values, not just a successful compile), by existing
/// test/CodeGen/VariantRecord/ and Storage/ content -- confirmed by a
/// dedicated research pass reading every candidate file on both sides, not
/// assumed from similar-looking Pascal:
///   - TheAlternativesOfAVariantShareTheirStorage is subsumed by
///     test/CodeGen/VariantRecord/the-alternatives-share-their-storage.pas
///   - AVariantWithNothingInItsAlternativesReservesNothing is subsumed by
///     test/CodeGen/VariantRecord/empty-alternatives-reserve-nothing.pas
///   - SemaSizesAPackedRecordWithoutPaddingOrATail is subsumed (as a strict
///     superset -- it needs both inter-field AND tail padding elided, with a
///     16-aligned member) by
///     test/CodeGen/Storage/a-packed-field-does-not-claim-an-alignment-it-cannot-keep.pas
///   - APackedRecordRoundTripsItsFields is subsumed by that same file
///
/// What remains here is a deliberate, permanent GoogleTest exception for two
/// reasons: 7 cases (TheScalarsAreTheSizeTheyAreLoweredAt through
/// WhatOnlyCodegenCanSizeIsNotGuessedAt) call Sema::byteSizeOf/byteAlignOf
/// directly on TypeContext-constructed Type objects with no compilation step
/// at all -- there is no SizeOf()/AlignOf() builtin exposed to Pascal, so
/// nothing here has a CLI-observable proxy even in principle. Two
/// (TwoIntegersOfOneWidthAreOneType, ThePredefinedIntegerIsTheInternedOne)
/// check raw C++ pointer identity on TypeContext's interning cache, which no
/// compiled program can observe. One
/// (RebindPointerDropsTheStaleCacheKeyAtTheOldAddress, issue #288) goes
/// further and placement-news over manually-owned storage so that two
/// successive Type objects provably share one address -- there is no Pascal
/// program that can make the allocator hand back a just-freed address on
/// demand (and an ASan build would refuse to, on purpose), so a compiled-
/// program proxy is not just unavailable today but cannot exist. Two
/// (TheDialectDecidesHowWideAnIntegerIs,
/// MaxintIsTheLargestValueTheDialectsIntegerHolds) are deliberately deferred
/// whole, not partially converted: -std=turbo doesn't exist yet, and the
/// ISO7185/EP halves are provably the same branch of the same ternary in
/// LangOptions::defaultIntWidth() today, so converting just those two would
/// add a full-pipeline test to reconfirm a fact that's structurally
/// guaranteed rather than actually at risk -- revisit both together as a
/// real 3-way (18/EP/Turbo) lit test once the Turbo milestone lands (see
/// project memory). One (ASubrangeSaysWhetherItsValuesCanBeNegative) checks
/// Type::IsSigned, which is confirmed (by grep across lib/ and include/) to
/// be write-only today -- nothing in the compiler reads it yet, so there is
/// no compiled-program behavior it could possibly affect; revisit once
/// something (again, almost certainly the Turbo widening rule) actually
/// consumes it. Two (CodegenReuse's cases) test that a SECOND in-process
/// Codegen object in the same process doesn't reuse the first's stale LLVM
/// types -- structurally unreachable from any CLI invocation, which always
/// compiles exactly once per process.

#include "plang/AST/TypeContext.h"
#include "plang/Basic/LangOptions.h"
#include "plang/CodeGen/CodeGen.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"
#include "plang/Sema/Sema.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <new>
#include <sstream>
#include <string>

using namespace plang;

namespace {

/// Compiles in process, returning the IR, or "" if any phase refused.  The
/// size-agreement check runs inside this, so a shape that breaks it aborts the
/// test binary rather than returning.
std::string irFor(const std::string& Src, LangOptions Opts = {}) {
    DiagnosticsEngine Diags{{}};
    SourceManager     SM;
    Scanner Sc(SM, "case.pas", Src, Diags, Opts);
    Parser  P(std::move(Sc), Diags, Opts);
    auto    Prog = P.parse();
    if (!Prog) return "";
    Sema S(Diags, Opts);
    if (!S.check(*Prog)) return "";
    std::ostringstream Out;
    Codegen CG(Opts);
    return CG.emit(*Prog, Out) ? Out.str() : std::string();
}

} // namespace

// ---------------------------------------------------------------------------
// Scalars
// ---------------------------------------------------------------------------

TEST(Storage, TheScalarsAreTheSizeTheyAreLoweredAt) {
    TypeContext C;
    EXPECT_EQ(Sema::byteSizeOf(*C.getInteger()), 8u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getReal()),    8u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getChar()),    1u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getBoolean()), 1u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getNil()),     8u);
    // EP §6.4.2.2: { double, double }.
    EXPECT_EQ(Sema::byteSizeOf(*C.getComplex()), 16u);
}

TEST(Storage, AnIntegerIsAsWideAsItWasAskedFor) {
    TypeContext C;
    EXPECT_EQ(Sema::byteSizeOf(*C.getInt(8,  true)), 1u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getInt(16, false)), 2u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getInt(32, true)), 4u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getInt(64, true)), 8u);
}

TEST(Storage, ASetIsTheWidthOfItsBitmaskAndAlignsToSixteen) {
    // It lowers to an i256, whose size is thirty-two bytes and whose alignment
    // is sixteen.  Getting the alignment wrong does not show up on the set: it
    // shows up as eight missing bytes of tail padding in a record that ends
    // with one, which is where it was actually found.
    TypeContext C;
    auto S = C.getSet(C.getChar(), /*packed=*/false);
    EXPECT_EQ(Sema::byteSizeOf(*S), 32u);
    EXPECT_EQ(Sema::byteAlignOf(*S), 16u);
}

TEST(Storage, AStringCarriesItsLengthInFront) {
    TypeContext C;
    // EP §6.4.3.3: { i64 length, [capacity x i8] }, rounded to the length's
    // own alignment.
    EXPECT_EQ(Sema::byteSizeOf(*C.getVarString(10)), 24u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getVarString(8)),  16u);
}

TEST(Storage, AnArrayIsItsCountTimesItsComponent) {
    TypeContext C;
    auto Idx = C.getSubrange(C.getInteger(), 1, 10);
    EXPECT_EQ(Sema::byteSizeOf(*C.getArray(Idx, C.getInteger(), false)), 80u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getArray(Idx, C.getChar(), false)),    10u);
    // Nested: the component is itself an array.
    auto Inner = C.getArray(Idx, C.getInteger(), false);
    EXPECT_EQ(Sema::byteSizeOf(*C.getArray(Idx, Inner, false)), 800u);
}

TEST(Storage, ASubrangeIsAsWideAsItsHostWhereNarrowingMeansSomething) {
    TypeContext C;
    // Over an integer, the host's width carries: this is what makes a Turbo
    // `0..255` of Byte one byte.
    auto Byte = C.getInt(8, false);
    EXPECT_EQ(Sema::byteSizeOf(*C.getSubrange(Byte, 0, 255)), 1u);
    EXPECT_EQ(Sema::byteSizeOf(*C.getSubrange(C.getInteger(), 1, 10)), 8u);
    // Over a char it is a full ordinal, which is what plang has always stored
    // one as.  Narrowing it moves ISO 7185 and Extended Pascal layouts, so it
    // waits for the dialect that needs it.
    EXPECT_EQ(Sema::byteSizeOf(*C.getSubrange(C.getChar(), 'a', 'z')), 8u);
}

TEST(Storage, WhatOnlyCodegenCanSizeIsNotGuessedAt) {
    // A conformant array's extent arrives with it and an undiscriminated
    // schema's with its discriminants.  Answering anything here would be
    // answering for some other instance.
    Type Conf;
    Conf.Kind = TypeKind::ConformantArray;
    EXPECT_EQ(Sema::byteSizeOf(Conf), std::nullopt);
    Type Schema;
    Schema.Kind = TypeKind::Schema;
    EXPECT_EQ(Sema::byteSizeOf(Schema), std::nullopt);
    Type Inst;
    Inst.Kind = TypeKind::SchemaInstance;
    EXPECT_EQ(Sema::byteSizeOf(Inst), std::nullopt);
}

// ---------------------------------------------------------------------------
// Interning
// ---------------------------------------------------------------------------

TEST(Storage, TwoIntegersOfOneWidthAreOneType) {
    // TypeContext::identical is a pointer comparison, so a second object for a
    // width would fail every identity check against the first -- silently,
    // since neither is wrong on its own.
    TypeContext C;
    EXPECT_EQ(C.getInt(16, true).get(), C.getInt(16, true).get());
    EXPECT_NE(C.getInt(16, true).get(), C.getInt(16, false).get());
    EXPECT_NE(C.getInt(16, true).get(), C.getInt(32, true).get());
}

TEST(Storage, ThePredefinedIntegerIsTheInternedOne) {
    // The trap this cache was most likely to fall into.  Sema binds TyInt as a
    // reference to what getInteger returns; if that were minted beside the
    // cache rather than through it, `integer` and `integer` would be two
    // objects and every identity check between them would fail.
    TypeContext C;
    EXPECT_EQ(C.getInteger().get(), C.getInt(64, true).get());
    EXPECT_TRUE(C.identical(C.getInteger(), C.getInt(64, true)));
}

TEST(Storage, RebindPointerDropsTheStaleCacheKeyAtTheOldAddress) {
    // Issue #288.  `^node` written before `node` is declared interns its
    // pointer type under its placeholder stub's address (getPointer keys on
    // addresses everywhere -- see the class comment).  rebindPointer then
    // re-files that entry under the real `node` type's address once it is
    // known, but used to leave the placeholder's entry behind.  Sema pins
    // every stub on the AST for the whole compilation (Sema::resolveType,
    // SemaType.cpp), so nothing frees a stub while its TypeContext is alive
    // today and the stale entry is inert in practice -- but that makes it a
    // latent hazard on that pinning invariant, not a safe design: free a
    // stub while its TypeContext lives on, and a later, unrelated Type
    // placed at the freed address would collide with the stale key and
    // getPointer would hand back the old pointer's identity instead of the
    // new object's own.
    //
    // A real free-then-reuse pair can't be manufactured on demand -- it is
    // exactly what the allocator owes no guarantee about, and an ASan build
    // actively prevents it by quarantining freed memory. So this test
    // controls the address itself: two Type objects are placement-new'd in
    // turn over one manually-owned buffer, which deterministically gives them
    // the same address without depending on allocator behavior at all.
    alignas(Type) unsigned char Storage[sizeof(Type)];
    // Declared after Storage, so C is destroyed (releasing every shared_ptr
    // its caches hold, including into Storage) before Storage's storage
    // duration ends -- see the reverse-construction-order destruction rule.
    TypeContext C;

    // The forward-declared pointer's placeholder, exactly as Sema's Phase 3a
    // builds one (Kind=Error, Name=the not-yet-declared type's name).
    auto* StubRaw = new (static_cast<void*>(Storage)) Type();
    StubRaw->Kind = TypeKind::Error;
    StubRaw->Name = "node";
    auto Stub = std::shared_ptr<Type>(StubRaw, [](Type* P) { P->~Type(); });

    // `^node`, resolved before `node`'s declaration: interned under the
    // stub's address.
    auto Ptr = C.getPointer(Stub);
    ASSERT_EQ(Ptr->PointeeType.get(), StubRaw);

    // `node`'s real declaration, arriving later in the same type section.
    auto Real = std::make_shared<Type>();
    Real->Kind = TypeKind::Record;
    Real->Name = "node";

    // Phase 3c's fixup: Ctx_.rebindPointer(T, Sym->Ty).
    C.rebindPointer(Ptr, Real);
    EXPECT_EQ(Ptr->PointeeType.get(), Real.get());
    // The primary purpose still holds: a later `^node` finds the same
    // pointer type rather than minting a second, non-identical one.
    EXPECT_TRUE(C.identical(C.getPointer(Real), Ptr));

    // Drop the stub -- rebindPointer's overwrite of Ptr->PointeeType left it
    // with no other owner -- and placement-new an unrelated second
    // forward-declared pointer's placeholder into the exact same bytes,
    // standing in for whatever a future refactor that stops pinning stubs on
    // the AST would let the allocator do for real.
    Stub.reset();
    auto* Stub2Raw = new (static_cast<void*>(Storage)) Type();
    Stub2Raw->Kind = TypeKind::Error;
    Stub2Raw->Name = "other";
    auto Stub2 = std::shared_ptr<Type>(Stub2Raw, [](Type* P) { P->~Type(); });
    ASSERT_EQ(static_cast<void*>(Stub2Raw), static_cast<void*>(StubRaw))
        << "test setup: Stub2 must land at the freed stub's exact address";

    // Before the fix, "ptr:<StubRaw's address>" still mapped to Ptr (which by
    // now points at Real), so this call returned Ptr -- a `^node` handed back
    // as `^other`.  After the fix it mints `^other`'s own pointer type.
    auto Ptr2 = C.getPointer(Stub2);
    EXPECT_NE(Ptr2.get(), Ptr.get());
    EXPECT_EQ(Ptr2->PointeeType.get(), Stub2Raw);
}

TEST(Storage, TheDialectDecidesHowWideAnIntegerIs) {
    LangOptions Iso;
    EXPECT_EQ(Iso.defaultIntWidth(), 64u);
    LangOptions Ep;
    Ep.Std = LangOptions::Standard::ISO10206;
    EXPECT_EQ(Ep.defaultIntWidth(), 64u);
    // -Mtp does not load objpas, so Turbo's Integer stays smallint and MaxInt
    // is 32767.  A program that overflows at 32767 on a real Turbo and not
    // here is not compiling as Turbo Pascal.
    LangOptions Tp;
    Tp.Std = LangOptions::Standard::Turbo;
    EXPECT_EQ(Tp.defaultIntWidth(), 16u);
}

// ---------------------------------------------------------------------------
// Compiling more than once in one process
//
// Not about storage, but this is where it was found: the cases above were the
// first thing to build two Codegen objects in one process, and the second one
// segfaulted.  The front end is a shared library so that other things can read
// Pascal -- a language server, for one -- and those compile repeatedly.  The
// driver never does, which is why nothing had noticed.
// ---------------------------------------------------------------------------

TEST(CodegenReuse, ASecondCompilationDoesNotUseTheFirstsTypes) {
    // The file record was cached in a function-local static, so it outlived the
    // LLVMContext that owned it.  A second compilation of a program with a file
    // parameter took the stale type and crashed building a null value of it —
    // in llvm::Constant::getNullValue, nowhere near the cache.
    const char* Src = "program p(output);\nbegin writeln(1) end.\n";
    const std::string First = irFor(Src);
    ASSERT_FALSE(First.empty());
    const std::string Second = irFor(Src);
    ASSERT_FALSE(Second.empty());
    // And the same program compiles to the same thing, which a per-compilation
    // cache gives for free and a shared one does not.
    EXPECT_EQ(First, Second);
}

TEST(CodegenReuse, ThirdAndFourthCompilationsAreStillFine) {
    // Once is a fluke; the cache is only reached the first time a file type is
    // wanted, so the failure needed exactly two.
    const char* Src = "program p(output);\nvar f: text;\nbegin rewrite(f) end.\n";
    for (int I = 0; I < 4; ++I)
        EXPECT_FALSE(irFor(Src).empty()) << "compilation " << I;
}

// ---------------------------------------------------------------------------
// Dialect-derived limits, deferred to the Turbo milestone
// ---------------------------------------------------------------------------

TEST(Storage, MaxintIsTheLargestValueTheDialectsIntegerHolds) {
    // Sema and codegen each know maxint, from the same LangOptions.  They have
    // to agree or `for i := 1 to maxint` wraps instead of terminating, and they
    // did not: codegen had INT64_MAX written into it and Sema had the same
    // constant a second time.
    const auto maxintOf = [](LangOptions::Standard Std) -> int64_t {
        LangOptions O;
        O.Std = Std;
        return static_cast<int64_t>(~0ULL >> (64 - O.defaultIntWidth() + 1));
    };
    EXPECT_EQ(maxintOf(LangOptions::Standard::ISO7185),  INT64_MAX);
    EXPECT_EQ(maxintOf(LangOptions::Standard::ISO10206), INT64_MAX);
    EXPECT_EQ(maxintOf(LangOptions::Standard::Turbo),    32767);
}

// ---------------------------------------------------------------------------
// Write-only metadata, waiting for its first reader
// ---------------------------------------------------------------------------

TEST(Storage, ASubrangeSaysWhetherItsValuesCanBeNegative) {
    // IsSigned defaults to true, and a subrange over a char, a boolean or an
    // enumeration has values that are ordinal numbers and never negative.  A
    // widening rule that read the default would sign-extend them.
    TypeContext C;
    EXPECT_FALSE(C.getSubrange(C.getChar(), 'a', 'z')->IsSigned);
    EXPECT_FALSE(C.getSubrange(C.getBoolean(), 0, 1)->IsSigned);
    // Over an integer it is the host's answer, which is what lets a Turbo
    // `0..255` of Byte widen without a sign.
    EXPECT_TRUE(C.getSubrange(C.getInteger(), -10, 10)->IsSigned);
    EXPECT_FALSE(C.getSubrange(C.getInt(8, /*Signed=*/false), 0, 255)->IsSigned);
    EXPECT_TRUE(C.getSubrange(C.getInt(16, /*Signed=*/true), -5, 5)->IsSigned);
}

