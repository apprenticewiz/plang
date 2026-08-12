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
#include "plang/CodeGen/Codegen.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"
#include "plang/Sema/Sema.h"

#include <gtest/gtest.h>

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
