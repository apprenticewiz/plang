/// switches_test.cpp — positional compiler-switch state
///
/// Turbo Pascal's `{$R+}` is textual: it applies from where it is written to
/// the end of the file, so one compilation can check one statement and not the
/// next.  Nothing on LangOptions can say that, and the answer lives in a table
/// of the places the state changed.
///
/// There is no directive scanner yet — that is Tier 1 — so the table is built
/// by hand here.  Building it by hand is also the only way to test the part
/// that matters most: that a location *before* the change still gets the old
/// state.  A directive can only ever be read in source order, so a scanner
/// cannot produce the case where the search has to go backwards.
///
/// Deferred wholesale from issue #43's GTest->lit migration, not a
/// permanent exception: SwitchTable/CompilerSwitches build a table by hand
/// in C++ and query it directly, which has no .pas source to write until
/// the Tier 1 `{$R+}`-style directive scanner exists to produce one for
/// real.  Two of SwitchConsumer's three cases are already meaningfully
/// duplicated by test/CodeGen/RuntimeChecks/*.pas's existing
/// -frange-checks/-fno-range-checks coverage and should be dropped, not
/// ported, once this file migrates; the third should migrate normally.
/// Revisit this whole file once Tier 1 ships.

#include "plang/Basic/SwitchTable.h"
#include "plang/Basic/LangOptions.h"
#include "plang/CodeGen/CodeGen.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"
#include "plang/Sema/Sema.h"

#include <gtest/gtest.h>

#include <functional>
#include <sstream>
#include <string>

using namespace plang;

namespace {

constexpr auto R = Switch::RangeChecks;

SourceLocation loc(unsigned Raw) { return SourceLocation::fromRaw(Raw); }

CompilerState with(Switch S, bool On) {
    CompilerState C;
    C.set(S, On);
    return C;
}

} // namespace

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

TEST(SwitchTable, AnEmptyTableAnswersWithTheDefault) {
    const SwitchTable T;
    EXPECT_TRUE(T.on(R, loc(100), with(R, true)));
    EXPECT_FALSE(T.on(R, loc(100), with(R, false)));
}

TEST(SwitchTable, ALocationBeforeEveryPointGetsTheDefault) {
    // The case a scanner cannot produce, and the reason the search is a search
    // rather than a running variable: a directive halfway down the file says
    // nothing about the text above it.
    SwitchTable T;
    T.record(loc(500), with(R, true));
    EXPECT_FALSE(T.on(R, loc(499), with(R, false)));
    EXPECT_TRUE(T.on(R, loc(500), with(R, false)));
    EXPECT_TRUE(T.on(R, loc(501), with(R, false)));
}

TEST(SwitchTable, TheLastPointAtOrBeforeTheLocationWins) {
    SwitchTable T;
    T.record(loc(10), with(R, true));
    T.record(loc(20), with(R, false));
    T.record(loc(30), with(R, true));
    const CompilerState D;  // everything off, so a hit is never the default
    EXPECT_FALSE(T.on(R, loc(9),  D));
    EXPECT_TRUE (T.on(R, loc(10), D));
    EXPECT_TRUE (T.on(R, loc(19), D));
    EXPECT_FALSE(T.on(R, loc(20), D));
    EXPECT_FALSE(T.on(R, loc(29), D));
    EXPECT_TRUE (T.on(R, loc(30), D));
    EXPECT_TRUE (T.on(R, loc(9999), D));
}

TEST(SwitchTable, TwoDirectivesAtOneLocationSettleToTheirCombinedEffect) {
    // `{$R+,I-}` is one comment and one location.  Recording two points there
    // would leave a state that no text is ever under.
    SwitchTable T;
    T.record(loc(10), with(R, true));
    CompilerState Both;
    Both.set(R, true);
    Both.set(Switch::IOChecks, false);
    T.record(loc(10), Both);
    EXPECT_EQ(T.size(), 1u);
    EXPECT_TRUE(T.on(R, loc(10), CompilerState{}));
    EXPECT_FALSE(T.on(Switch::IOChecks, loc(10), CompilerState{}));
}

TEST(SwitchTable, ANodeWithNoLocationGetsTheDefault) {
    // The parser synthesizes nodes, and a synthesized node has no place in the
    // source to ask about.  Raw zero is also the smallest offset there is, so
    // without this it would collect the first point in the file.
    SwitchTable T;
    T.record(loc(1), with(R, true));
    EXPECT_FALSE(T.on(R, SourceLocation{}, with(R, false)));
}

TEST(SwitchTable, AnIncludedBufferIsFoundByItsOwnPoints) {
    // Locations are offsets into one space covering every buffer, and an
    // included file gets a *later* stretch of it than the file that included
    // it.  So the text after an include sits at a *lower* offset than the
    // included text, and a plain backwards search would hand it the include's
    // final state.
    //
    // What keeps it right is a point at offset zero of the pushed buffer and
    // another at the parent's resume offset.  This is the shape of that, which
    // is what the scanner will have to produce.
    SwitchTable T;
    T.record(loc(100), with(R, true));    // {$R+} in the parent, before the include
    T.record(loc(900), with(R, false));   // start of the included buffer: {$R-}
    T.record(loc(950), with(R, true));    // {$R+} inside the include
    const CompilerState D;

    EXPECT_TRUE (T.on(R, loc(120), D));   // parent, after its own directive
    EXPECT_FALSE(T.on(R, loc(910), D));   // inside the include, before its {$R+}
    EXPECT_TRUE (T.on(R, loc(960), D));   // inside the include, after it
}

TEST(SwitchTable, TheParentResumesWhereItLeftOffAfterAnInclude) {
    // The other half of the case above: the resume point is what stops the
    // include's last state leaking into the rest of the parent.  Without the
    // point at 200 the text at 210 would find the point at 950 and be checked.
    SwitchTable T;
    T.record(loc(100), with(R, false));   // parent: {$R-}
    T.record(loc(900), with(R, false));   // include starts, inheriting
    T.record(loc(950), with(R, true));    // {$R+} inside the include
    T.record(loc(200), with(R, false));   // parent resumes -- recorded on pop
    const CompilerState D;
    // Recorded out of offset order, which is the order they happen in.  The
    // table takes them in source order, so the resume point replaces nothing
    // and the parent is still unchecked.
    EXPECT_FALSE(T.on(R, loc(210), D));
}

// ---------------------------------------------------------------------------
// The spellings
// ---------------------------------------------------------------------------

TEST(CompilerSwitches, ASwitchIsFoundByEitherSpelling) {
    EXPECT_EQ(switchFromLetter('R'), Switch::RangeChecks);
    EXPECT_EQ(switchFromLetter('r'), Switch::RangeChecks);
    EXPECT_EQ(switchFromLongName("rangechecks"), Switch::RangeChecks);
    EXPECT_EQ(switchFromLetter('$'), std::nullopt);
    EXPECT_EQ(switchFromLongName("nosuchswitch"), std::nullopt);
}

TEST(CompilerSwitches, EveryLetteredSwitchIsDistinctAndEveryOneHasALongName) {
    // A duplicated letter would make `{$IFOPT}` answer about the wrong switch,
    // silently, for one of the two.  '\0' is not a duplicate-in-waiting: it is
    // ObjectChecks/Goto's honest answer (see CompilerSwitches.def's own
    // comment) that they have no single-letter spelling in real Turbo/FPC at
    // all, and switchFromLetter can never be asked about '\0' for real, since
    // a directive name reaching it is always at least one alphabetic
    // character.
    std::string Letters;
    for (unsigned I = 0; I < NumSwitches; ++I) {
        const auto S = static_cast<Switch>(I);
        ASSERT_FALSE(switchLongName(S).empty());
        if (switchLetter(S) == '\0') continue;
        EXPECT_EQ(Letters.find(switchLetter(S)), std::string::npos)
            << "two switches spelled '" << switchLetter(S) << "'";
        Letters += switchLetter(S);
    }
}

TEST(CompilerSwitches, TurboStartsWithRangeCheckingOffAndPlangDoesNot) {
    // The two defaults differ, which is why the table's default comes from
    // LangOptions and not from the .def: -std=iso7185 checks by default, and
    // Turbo Pascal does not.
    EXPECT_FALSE(CompilerState::turboDefaults().on(R));
    EXPECT_TRUE(LangOptions{}.defaultSwitches().on(R));
}

TEST(CompilerSwitches, TheCommandLineIsWhatADirectiveOverrides) {
    LangOptions Opts;
    Opts.RangeChecks = false;
    EXPECT_FALSE(Opts.switchOn(R, loc(10)));

    auto T = std::make_shared<SwitchTable>();
    T->record(loc(5), with(R, true));
    Opts.Switches = T;
    EXPECT_TRUE(Opts.switchOn(R, loc(10)));
    EXPECT_FALSE(Opts.switchOn(R, loc(4)));  // before it: still the flag
}

TEST(CompilerSwitches, WithNoTableTheAnswerIsTheFlagAndNothingIsSearched) {
    // What keeps ISO 7185 and Extended Pascal on the path they were on: no
    // directives, so no table, so this is the only branch they take.
    LangOptions Opts;
    EXPECT_EQ(Opts.Switches, nullptr);
    EXPECT_TRUE(Opts.switchOn(R, loc(1)));
    Opts.RangeChecks = false;
    EXPECT_FALSE(Opts.switchOn(R, loc(1)));
}

// ---------------------------------------------------------------------------
// The consumer
// ---------------------------------------------------------------------------

namespace {

/// Compiles \p Src in process and returns the IR, with \p Build given a chance
/// to put a switch table on the options first.
std::string irFor(const std::string& Src,
                  const std::function<void(LangOptions&, const SourceManager&,
                                           FileID)>& Build = nullptr) {
    DiagnosticsEngine Diags{{}};
    SourceManager     SM;
    LangOptions       Opts;

    // The buffer-scanning constructor, not the file one: these have no file,
    // and the offsets the table is keyed by have to be the ones the scanner
    // actually handed the parser.
    Scanner Sc(SM, "case.pas", Src, Diags, Opts);
    const FileID FID = SM.getFileID(SourceLocation::fromRaw(1));
    Parser  P(std::move(Sc), Diags, Opts);
    auto    Prog = P.parse();
    if (!Prog) return "";
    Sema S(Diags, Opts);
    if (!S.check(*Prog)) return "";

    if (Build) Build(Opts, SM, FID);
    std::ostringstream Out;
    Codegen CG(Opts);
    return CG.emit(*Prog, Out) ? Out.str() : std::string();
}

int countOf(const std::string& Hay, const std::string& Needle) {
    int N = 0;
    for (size_t P = Hay.find(Needle); P != std::string::npos;
         P = Hay.find(Needle, P + Needle.size()))
        ++N;
    return N;
}

/// Two indexed assignments, so that a switch between them changes one and not
/// the other.
constexpr const char* TwoIndexed =
    "program p;\n"
    "var a: array[1..3] of integer; i: integer;\n"
    "begin i := 2; a[i] := 1; a[i] := 2 end.\n";

} // namespace

TEST(SwitchConsumer, BothIndexesAreCheckedByDefault) {
    EXPECT_EQ(countOf(irFor(TwoIndexed), "call void @plang_err_index"), 2);
}

TEST(SwitchConsumer, ASwitchPartWayThroughChecksOneAndNotTheOther) {
    // The whole reason for the table.  A translation-unit flag can produce two
    // checks or none; this produces the first and not the second, from one
    // compilation of one file.
    const std::string IR = irFor(TwoIndexed, [](LangOptions& Opts,
                                                const SourceManager& SM,
                                                FileID FID) {
        // Halfway: after the first `a[i]` and before the second.  Found by
        // searching the buffer rather than counted out, so that editing the
        // program above cannot silently move the point off the boundary.
        const std::string_view Text = SM.getBufferData(FID);
        const size_t First  = Text.find("a[i]");
        const size_t Second = Text.find("a[i]", First + 1);
        ASSERT_NE(Second, std::string_view::npos);

        auto T = std::make_shared<SwitchTable>();
        CompilerState Off;
        Off.set(Switch::RangeChecks, false);
        T->record(SM.getLocForOffset(FID, Second), Off);
        Opts.Switches = T;
    });
    EXPECT_EQ(countOf(IR, "call void @plang_err_index"), 1) << IR;
}

TEST(SwitchConsumer, ATableThatSaysNothingLeavesTheFlagInCharge) {
    const std::string IR = irFor(TwoIndexed, [](LangOptions& Opts,
                                                const SourceManager&, FileID) {
        Opts.Switches = std::make_shared<SwitchTable>();  // empty
    });
    EXPECT_EQ(countOf(IR, "call void @plang_err_index"), 2);
}
