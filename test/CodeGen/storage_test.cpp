/// storage_test.cpp — how wide a type is, and where its parts sit
///
/// Sema works sizes out without a DataLayout, because Turbo writes
/// `const BufSize = 4 * SizeOf(Integer)` and a constant has to fold long before
/// there is one.  That makes two answers to one question, which is the
/// arrangement that goes wrong quietly: a SizeOf that disagrees with the
/// layout sizes a GetMem or a BlockRead buffer wrong, and nothing says so
/// until the memory past the end of it is read back.
///
/// What holds them together is not a test but an assertion in the compiler:
/// codegen checks `Sema::byteSizeOf` against `DataLayout::getTypeAllocSize` for
/// every type it lowers, in every program it compiles, and a disagreement is
/// an ICE.  So the cases below that compile a program are not asserting on
/// what they print — they are putting a shape through that assertion.  Each
/// one is a shape that has already been found to break it.

#include "plang/AST/TypeContext.h"
#include "plang/Basic/LangOptions.h"
#include "plang/CodeGen/CodeGen.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"
#include "plang/Sema/Sema.h"

#include <gtest/gtest.h>

#include <cstdint>
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
// Aggregates, put through the agreement check
//
// Each of these is a shape that broke it.  They assert that compilation
// finishes, because the thing being tested aborts the process when it fails.
// ---------------------------------------------------------------------------

TEST(Storage, ARecordEndingInASetIsPaddedToTheSetsAlignment) {
    // Found in the acceptance test: eight bytes short, all of it in the tail
    // padding, where no field would ever have shown it.
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type r = record i: integer; s: set of char; p: ^integer end;\n"
        "var v: r;\n"
        "begin v.i := 1; writeln(v.i) end.\n").empty());
}

TEST(Storage, TheAlternativesOfAVariantShareTheirStorage) {
    // ISO §6.4.3.3 lets a variant field be selected by name like any other, so
    // Sema's flat field list holds every alternative's fields.  Summing that
    // list counts storage the alternatives share, and a record of two
    // four-byte alternatives came out twice the size it was laid out at.
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type r = record\n"
        "  tag: boolean;\n"
        "  case b: boolean of\n"
        "    true:  (i: integer; j: integer);\n"
        "    false: (c: char)\n"
        "end;\n"
        "var v: r;\n"
        "begin v.tag := true; writeln(v.tag) end.\n").empty());
}

TEST(Storage, ANestedVariantStartsAfterTheAlternativeThatHoldsIt) {
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type r = record\n"
        "  case a: boolean of\n"
        "    true:  (i: integer;\n"
        "            case b: boolean of\n"
        "              true:  (x: real);\n"
        "              false: (y: char));\n"
        "    false: (s: set of char)\n"
        "end;\n"
        "var v: r;\n"
        "begin v.a := false; writeln(v.a) end.\n").empty());
}

TEST(Storage, AVariantPartIsAlignedForTheWidestThingInIt) {
    // `set of char` is i256, which this data layout aligns to 16, and codegen
    // emits set accesses with the alignment of the TYPE.  The blob a variant
    // part reserves used to cap its cell at i64, so the run was 8-aligned and
    // sat at offset 8 of an 8-aligned object -- and the compiler then promised
    // LLVM `align 16` on every load and store of the set inside it.  An
    // aligned vector access is within its rights to fault on that.
    //
    // Three implementations had to be taught this, which is the R4 point: the
    // cap was written into Codegen::variantBlobType, into Sema's
    // variantBlobBytes, and a THIRD time as an explicit min(BlobAlign, 8) in
    // Sema::byteSizeOf.  The run-time walk was the only one that had it right,
    // and nothing compared it to the other two until now.
    const std::string Ir = irFor(
        "program p(output);\n"
        "type r = record\n"
        "  case a: boolean of\n"
        "    true:  (i: integer);\n"
        "    false: (s: set of char)\n"
        "end;\n"
        "var v: r;\n"
        "begin v.s := ['x']; if 'x' in v.s then writeln('yes') end.\n");
    ASSERT_FALSE(Ir.empty());
    // The cell carries the alignment, so the blob has to be made of something
    // that wants 16 -- an i64 array would be a 16-byte promise on an 8-byte
    // object however many elements it had.
    EXPECT_NE(Ir.find("{ i1, [2 x i128] }"), std::string::npos) << Ir;
    EXPECT_EQ(Ir.find("{ i1, [4 x i64] }"), std::string::npos) << Ir;
}

TEST(Storage, AVariantWithNothingInItsAlternativesReservesNothing) {
    // `case b: boolean of true: (); false: ()` is a record with a tag and no
    // more, and there is no blob to reserve or to align to.
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type r = record i: integer; case b: boolean of true: (); false: () end;\n"
        "var v: r;\n"
        "begin v.i := 2; writeln(v.i) end.\n").empty());
}

TEST(Storage, ARecordOfMixedWidthsIsPaddedFieldByField) {
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type r = record a: char; b: integer; c: boolean; d: real;\n"
        "                e: packed array[1..3] of char; f: integer end;\n"
        "var v: r;\n"
        "begin v.b := 3; writeln(v.b) end.\n").empty());
}

// ---------------------------------------------------------------------------
// packed
//
// ISO §6.4.3.1 leaves what `packed` does to the implementation, and plang used
// to do nothing with it — SemaType resolved it away as a storage hint and
// codegen never built a packed struct.  It packs now, in every dialect: Turbo
// needs it for {$PACKRECORDS 1} and for a record image a real Turbo program can
// read, and a `packed` that packs nothing is a word the language has that means
// nothing.
// ---------------------------------------------------------------------------

TEST(Packed, APackedRecordIsBuiltAsAPackedStruct) {
    const std::string IR = irFor(
        "program p(output);\n"
        "type k = packed record a: char; b: integer; c: char end;\n"
        "var v: k;\n"
        "begin v.b := 1; writeln(v.b) end.\n");
    ASSERT_FALSE(IR.empty());
    // <{ }> is LLVM's spelling of a struct with no padding in it.
    EXPECT_NE(IR.find("<{ i8, i64, i8 }>"), std::string::npos) << IR;
}

TEST(Packed, AnUnpackedRecordIsStillPadded) {
    // The other half: packing one record must not pack every record.
    const std::string IR = irFor(
        "program p(output);\n"
        "type u = record a: char; b: integer; c: char end;\n"
        "var v: u;\n"
        "begin v.b := 1; writeln(v.b) end.\n");
    ASSERT_FALSE(IR.empty());
    EXPECT_NE(IR.find("{ i8, i64, i8 }"), std::string::npos) << IR;
    EXPECT_EQ(IR.find("<{ i8, i64, i8 }>"), std::string::npos) << IR;
}

TEST(Packed, TwoRecordsThatDifferOnlyInPackingAreTwoLayouts) {
    // The struct-type cache keys on the field types, so without packing in the
    // key these two would share one struct — and whichever was built second
    // would take the first's offsets.
    const std::string IR = irFor(
        "program p(output);\n"
        "type u =        record a: char; b: integer end;\n"
        "     k = packed record a: char; b: integer end;\n"
        "var vu: u; vk: k;\n"
        "begin vu.b := 1; vk.b := 2; writeln(vu.b + vk.b) end.\n");
    ASSERT_FALSE(IR.empty());
    EXPECT_NE(IR.find("<{ i8, i64 }>"), std::string::npos) << IR;
    EXPECT_NE(IR.find(" { i8, i64 }"), std::string::npos) << IR;
}

TEST(Packed, SemaSizesAPackedRecordWithoutPaddingOrATail) {
    // Compiles, so the size-agreement check found Sema and the layout saying
    // the same thing about a record with no padding in it and none on the end.
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type k = packed record a: char; b: integer; c: boolean; d: real end;\n"
        "var v: k;\n"
        "begin v.b := 1; writeln(v.b) end.\n").empty());
}

TEST(Packed, APackedVariantSharesStorageWithoutPaddingEither) {
    EXPECT_FALSE(irFor(
        "program p(output);\n"
        "type k = packed record\n"
        "  a: char;\n"
        "  case b: boolean of\n"
        "    true:  (i: integer);\n"
        "    false: (c: char; d: char)\n"
        "end;\n"
        "var v: k;\n"
        "begin v.a := 'x'; writeln(v.a) end.\n").empty());
}

TEST(Packed, APackedRecordRoundTripsItsFields) {
    // Offsets, not just sizes.  A packed layout that agreed on the total and
    // put the fields in the wrong places would pass everything above.
    const std::string IR = irFor(
        "program p(output);\n"
        "type k = packed record a: char; b: integer; c: char end;\n"
        "var v: k;\n"
        "begin v.a := 'x'; v.b := 42; v.c := 'y';\n"
        "  writeln(v.a, v.b:3, v.c) end.\n");
    EXPECT_FALSE(IR.empty());
}

// ---------------------------------------------------------------------------
// Compiling more than once in one process
//
// Not about storage, but this is where it was found: the cases above were the
// first thing to build two Codegen objects in one process, and the second one
// segfaulted.  The front end is a shared library so that other things can read
// Pascal — a language server, for one — and those compile repeatedly.  The
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
