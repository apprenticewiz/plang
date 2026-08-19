/// driver_test.cpp — the driver and the command line
///
/// What plang does before and after it compiles anything: which arguments it
/// accepts, what it says about the ones it does not, how a diagnostic is
/// printed, which of them are warnings, when they are colored, and which
/// dialect a flag selects.  The programs here are usually one line, because
/// the program is not what is being tested.

#include "DriverHarness.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// The case directory
//
// The harness the rest of these suites are built on.  A case that needs more
// than one file -- a program that opens a data file by name, a multi-unit
// build, an include directive -- needs the name in the source and the name on
// disk to agree, and needs no other case running under `ctest -j` to be able
// to reach either.
// ---------------------------------------------------------------------------

TEST(CaseDirectory, AProgramRunsWithTheCaseDirectoryAsItsWorkingDirectory) {
    // A relative name in the source has to mean a file in this case's own
    // directory.  Before this, the program ran wherever ctest happened to be,
    // so a case that wrote 'r.txt' wrote it into the build tree, left it
    // there, and would have read the previous run's copy had it not been
    // overwritten first.
    CaseDir C;
    C.write("case.pas",
        "program p(output);\n"
        "var f: text;\n"
        "begin rewrite(f, 'made.txt'); writeln(f, 'written'); close(f) end.\n");
    const auto R = C.compileAndRunFile("case.pas");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_TRUE(C.exists("made.txt"));
    EXPECT_EQ(C.read("made.txt"), "written\n");
}

TEST(CaseDirectory, TwoCasesCannotSeeEachOthersFiles) {
    // What the per-case directory is for.  Both write the same name, and each
    // has to read back its own.
    CaseDir A, B;
    EXPECT_NE(A.path(), B.path());
    A.write("shared.txt", "from A");
    B.write("shared.txt", "from B");
    EXPECT_EQ(A.read("shared.txt"), "from A");
    EXPECT_EQ(B.read("shared.txt"), "from B");
}

TEST(CaseDirectory, AFileMayGoInASubdirectory) {
    // An include-path or module-path case has to be able to put a file
    // somewhere the compiler will not find without being told where to look.
    CaseDir C;
    C.write("inc/deep/thing.txt", "found");
    EXPECT_TRUE(C.exists("inc/deep/thing.txt"));
    EXPECT_EQ(C.read("inc/deep/thing.txt"), "found");
    EXPECT_FALSE(C.exists("thing.txt"));
}

TEST(CaseDirectory, ANamedSourceIsCompiledUnderTheNameItWasGiven) {
    // compileAndRun names the source itself, which is why a diagnostic from it
    // always says case.pas.  A case about a named file needs the compiler to
    // have been given the name the case chose, and the diagnostic is where
    // that shows.
    CaseDir C;
    C.write("mine.pas", "program p; begin x := 1 end.\n");
    const auto [Rc, Out] = C.runPlangIn("-c mine.pas");
    EXPECT_NE(Rc, 0);
    EXPECT_NE(Out.find("mine.pas"), std::string::npos) << Out;
}

TEST(CaseDirectory, TheDirectoryGoesAwayWithTheCase) {
    // Cases run in the thousands; one that left its directory behind would
    // fill /tmp over an afternoon rather than fail anything.
    std::string Path;
    {
        CaseDir C;
        Path = C.path();
        C.write("case.pas", "program p; begin end.\n");
        EXPECT_TRUE(std::filesystem::exists(Path));
    }
    EXPECT_FALSE(std::filesystem::exists(Path));
}

// ---------------------------------------------------------------------------
// plang -pc1 (front-end mode) tests
// ---------------------------------------------------------------------------

TEST(PC1, MissingInputFileOneError) {
    std::string Out = runPC1("/nonexistent_file_plang_test.pas");
    EXPECT_EQ(diagCount(Out), 1) << "expected exactly one error, got:\n" << Out;
}

TEST(PC1, MissingInputFileCorrectMessage) {
    std::string Out = runPC1("/nonexistent_file_plang_test.pas");
    EXPECT_NE(Out.find("no such file or directory"), std::string::npos)
        << "expected 'no such file or directory' in output, got:\n" << Out;
}

TEST(PC1, MissingInputFileNoCascadeErrors) {
    std::string Out = runPC1("/nonexistent_file_plang_test.pas");
    EXPECT_EQ(Out.find("expected"), std::string::npos)
        << "cascade parse errors should not appear, got:\n" << Out;
}

// ---------------------------------------------------------------------------
// plang driver tests
// ---------------------------------------------------------------------------

TEST(Driver, MissingInputFileOneError) {
    std::string Out = runPlang("/nonexistent_file_plang_test.pas");
    EXPECT_EQ(diagCount(Out), 1)
        << "expected exactly one error, got:\n" << Out;
}

TEST(Driver, DashGStillCompilesAndRunsCorrectly) {
    // -g used to be forwarded to llc, which has no such option, so EVERY -g
    // compile failed with "llc: Unknown command line argument '-g'" -- fixed
    // by not forwarding it (debug info travels in the IR's own metadata, not
    // as a code-generator flag; see DashGProducesDebugMetadataInTheIR for
    // confirmation llc picks it up from there without any flag of its own).
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln('hi') end.\n", "-g");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi\n");
    EXPECT_EQ(R.Stderr.find("Unknown command line argument"),
              std::string::npos) << R.Stderr;
    // The old "-g accepted but produces nothing" warning is retired now that
    // -g actually does something; nothing should still be saying it.
    EXPECT_EQ(R.Stderr.find("does not emit debug information"),
              std::string::npos) << R.Stderr;
}

TEST(Driver, DashGProducesDebugMetadataInTheIR) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "begin writeln('hi') end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"!llvm.dbg.cu", "DICompileUnit",
                                     "DIFile", "Debug Info Version"}))
        << R.IR;
}

TEST(Driver, WithoutDashGNoDebugMetadataAppears) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "begin writeln('hi') end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsNone(R.IR, {"!llvm.dbg.cu", "DICompileUnit"}))
        << R.IR;
}

TEST(Driver, DashGCompilesMultipleTopLevelProceduresWithoutAVerifierFailure) {
    // builder's current debug location is not function-scoped state; it
    // survives across whichever function was emitted right before this one.
    // Compiling addone() then main() under -g used to attach main's own
    // prologue instructions (emitFileParamBinds's plang_bind_std call, run
    // before main's own first statement) to addone's DISubprogram, which
    // the verifier rejects outright: "!dbg attachment points at wrong
    // subprogram for function".  Every -g compile of more than one
    // procedure failed to compile at all.
    auto R = compileAndRun(
        "program p(output);\n"
        "var x, y: integer;\n"
        "\n"
        "procedure addone(var n: integer);\n"
        "begin\n"
        "  n := n + 1\n"
        "end;\n"
        "\n"
        "begin\n"
        "  x := 10;\n"
        "  addone(x);\n"
        "  y := x * 2;\n"
        "  writeln(y)\n"
        "end.\n", "-g");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22\n");
    EXPECT_EQ(R.Stderr.find("verification failed"), std::string::npos)
        << R.Stderr;
}

TEST(Driver, DashGGivesANestedProcedureCallItsOwnCallSiteLine) {
    // createEntryAlloca hoists an alloca to the entry block via
    // saveIP/SetInsertPoint/restoreIP; SetInsertPoint at an *existing*
    // instruction (entry.begin(), since the prologue's own allocas are
    // already there) also adopts that instruction's !dbg, and plain
    // restoreIP does not restore the debug location back afterward.
    // outer's static-link frame for its call to inner is built this way,
    // so the call inherited outer's own prologue line (4) instead of its
    // real call site (14, where 'inner;' is written) -- confirmed with a
    // real gdb backtrace showing "pas_outer () at ...:4" before the fix,
    // ":14" after.
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var x: integer;\n"
        "\n"
        "procedure outer;\n"
        "  var y: integer;\n"
        "\n"
        "  procedure inner;\n"
        "  begin\n"
        "    y := y + 1\n"
        "  end;\n"
        "\n"
        "begin\n"
        "  y := 5;\n"
        "  inner;\n"
        "  writeln(y)\n"
        "end;\n"
        "\n"
        "begin\n"
        "  x := 1;\n"
        "  outer;\n"
        "  writeln(x)\n"
        "end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_EQ(irDbgLineOf(R.IR, "call void @\"pas_outer$inner\""), 14) << R.IR;
}

TEST(Driver, DashGGivesAProceduralParameterThunkNoStrayDebugLocation) {
    // procParamThunk builds a whole separate function (the trampoline EP
    // §6.6.3.1 needs when a procedure is passed as a procedural parameter)
    // via SetInsertPoint(BasicBlock*), which does not touch the debug
    // location at all -- so the thunk's own instructions inherited
    // whatever the *caller's* current location happened to be, silently
    // scoped to the caller's DISubprogram rather than the thunk's own
    // (which does not exist: the thunk has no Pascal-level source
    // identity, so it gets none, correctly, once cleared). The verifier
    // does not catch this specific case -- a function with no DISubprogram
    // at all is not checked against the scope its instructions claim --
    // so this was a silent correctness gap, not a compile failure.
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var g: integer;\n"
        "\n"
        "procedure hello;\n"
        "begin\n"
        "  g := g + 1\n"
        "end;\n"
        "\n"
        "procedure invoke(procedure act);\n"
        "begin\n"
        "  act\n"
        "end;\n"
        "\n"
        "begin\n"
        "  g := 0;\n"
        "  invoke(hello);\n"
        "  writeln(g)\n"
        "end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    // @pas_hello.asparam also appears at its call site (a function-pointer
    // argument), so this has to anchor on the definition specifically, not
    // the first occurrence of the bare name.
    const auto ThunkStart = R.IR.find("define internal void @pas_hello.asparam");
    ASSERT_NE(ThunkStart, std::string::npos) << R.IR;
    const auto ThunkEnd = R.IR.find("}", ThunkStart);
    EXPECT_EQ(R.IR.find("!dbg", ThunkStart), std::string::npos)
        << R.IR.substr(ThunkStart, ThunkEnd - ThunkStart);
}

TEST(Driver, DashGGivesAGlobalVariableADIGlobalVariableExpression) {
    // Pascal's own program-level 'var' declarations compile to LLVM
    // GlobalVariables, not allocas -- the plan's own createAutoVariable/
    // insertDeclare design (for a *local*) does not apply to them at all;
    // they need createGlobalVariableExpression + GlobalVariable::
    // addDebugInfo instead, found only by checking what the most common
    // Pascal program shape (a plain `var x: integer` under the program,
    // no procedures) actually compiles down to.
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var x: integer;\n"
        "begin x := 1; writeln(x) end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"DIGlobalVariableExpression",
                                     "!DIGlobalVariable(name: \"x\""}))
        << R.IR;
}

TEST(Driver, DashGGivesALocalAndAParameterTheirOwnDeclareRecords) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var x: integer;\n"
        "\n"
        "procedure addone(var n: integer);\n"
        "var doubled: integer;\n"
        "begin\n"
        "  doubled := n * 2;\n"
        "  n := n + 1\n"
        "end;\n"
        "\n"
        "begin\n"
        "  x := 10;\n"
        "  addone(x);\n"
        "  writeln(x)\n"
        "end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    // #dbg_declare is LLVM 22's post-"RemoveDIs" textual form for what used
    // to print as a call to the llvm.dbg.declare intrinsic.
    EXPECT_TRUE(irContainsAll(R.IR, {"#dbg_declare", "!DILocalVariable(name: \"n\"",
                                     "!DILocalVariable(name: \"doubled\""}))
        << R.IR;
}

TEST(Driver, DashGGivesEachScalarKindItsOwnDIType) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var i: integer; r: real; b: boolean; c: char;\n"
        "begin\n"
        "  i := 1; r := 1.0; b := true; c := 'x';\n"
        "  writeln(i)\n"
        "end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {
        "!DIBasicType(name: \"integer\", size: 64, encoding: DW_ATE_signed)",
        "!DIBasicType(name: \"real\", size: 64, encoding: DW_ATE_float)",
        "!DIBasicType(name: \"boolean\", size: 8, encoding: DW_ATE_boolean)",
        "!DIBasicType(name: \"char\", size: 8, encoding: DW_ATE_unsigned_char)",
    })) << R.IR;
}

TEST(Driver, DashGGivesAnEnumItsEnumeratorNamesInDeclarationOrder) {
    // EnumValues (Type.h) has no separately-stored ordinal per name -- each
    // name's own index IS its ordinal -- so this also stands in for
    // confirming that reading, not just the DIEnumerator call itself.
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "type color = (red, green, blue);\n"
        "var c: color;\n"
        "begin c := green; writeln(ord(c)) end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {
        "DW_TAG_enumeration_type, name: \"color\"",
        "!DIEnumerator(name: \"red\", value: 0)",
        "!DIEnumerator(name: \"green\", value: 1)",
        "!DIEnumerator(name: \"blue\", value: 2)",
    })) << R.IR;
}

TEST(Driver, DashGGivesAPointerACorrectlyTypedPointeeAndACompositePointeeNone) {
    // ^integer needs its pointee's own DIType; ^rec (a pointer to a
    // composite, out of scope for this pass) still needs to compile
    // cleanly, with a null pointee rather than a placeholder invented for
    // the occasion -- createPointerType accepts that, matching a C void*.
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "type rec = record f: integer end;\n"
        "var ip: ^integer; rp: ^rec;\n"
        "begin\n"
        "  new(ip); ip^ := 1;\n"
        "  new(rp); rp^.f := 2;\n"
        "  writeln(ip^)\n"
        "end.\n", "-g");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR,
        {"!DIDerivedType(tag: DW_TAG_pointer_type, baseType: ",
         "size: 64)"})) << R.IR;
    // rp's own pointer DIType has no baseType field at all (a null pointee
    // is simply omitted from the printed form, not printed as baseType:
    // null), which is what this is really confirming compiles cleanly.
}

TEST(Driver, MissingInputFileNoCommandFailed) {
    std::string Out = runPlang("/nonexistent_file_plang_test.pas");
    EXPECT_EQ(Out.find("command failed"), std::string::npos)
        << "'command failed' should not appear, got:\n" << Out;
}

TEST(Driver, MissingInputFileCorrectMessage) {
    std::string Out = runPlang("/nonexistent_file_plang_test.pas");
    EXPECT_NE(Out.find("no such file or directory"), std::string::npos)
        << "expected 'no such file or directory' in output, got:\n" << Out;
}

// ---------------------------------------------------------------------------
// Diagnostic language
//
// qps_ploc is not a language: it is the English wrapped in [! !], generated
// alongside en_US.po.  It is what lets a test tell a build that read a catalog
// from one that fell back to English -- which is otherwise indistinguishable,
// because falling back is deliberate, total and silent.
//
// These cases run against the build tree, where PinnedLocale has set LC_ALL=C
// for every child, so anything they see comes from the flag and not from the
// machine the suite happens to be running on.
// ---------------------------------------------------------------------------

TEST(DiagnosticLanguage, TheDefaultIsTheBuiltInEnglish) {
    auto R = compileAndRun("program p(output);\nbegin x := 1 end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_EQ(R.Stderr.find("[!"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("not an assignable variable"), std::string::npos)
        << R.Stderr;
}

TEST(DiagnosticLanguage, TheFlagReachesTheFrontEnd) {
    // The message comes from -pc1, a separate process the driver spawns, so
    // this is really asking whether the option was forwarded and understood.
    auto R = compileAndRun("program p(output);\nbegin x := 1 end.\n",
                           "-fdiagnostics-language=qps_ploc");
    EXPECT_NE(R.Stderr.find("[!"), std::string::npos) << R.Stderr;
}

TEST(DiagnosticLanguage, TheFlagReachesTheDriverToo) {
    // err_no_input_files is the driver's own, reported before -pc1 is spawned.
    std::string Out = runPlang("-fdiagnostics-language=qps_ploc");
    EXPECT_NE(Out.find("[!no input files!]"), std::string::npos) << Out;
}

TEST(DiagnosticLanguage, ForwardingItDoesNotMakeTheFrontEndComplain) {
    // The front end parses with a hand-written chain ending in "unrecognized
    // argument".  The driver forwards this option, so without an arm for it
    // there every single compile would warn about it.
    auto R = compileAndRun("program p(output);\nbegin writeln('ok') end.\n",
                           "-fdiagnostics-language=qps_ploc");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("unrecognized"), std::string::npos) << R.Stderr;
}

TEST(DiagnosticLanguage, CMeansTheBuiltInEnglish) {
    auto R = compileAndRun("program p(output);\nbegin x := 1 end.\n",
                           "-fdiagnostics-language=C");
    EXPECT_EQ(R.Stderr.find("[!"), std::string::npos) << R.Stderr;
}

TEST(DiagnosticLanguage, AnUnknownLanguageIsNotAnError) {
    // Everything that can go wrong ends in English rather than in a failure:
    // a compiler that would not compile because a translation is missing would
    // be a worse compiler.
    auto R = compileAndRun("program p(output);\nbegin writeln('ok') end.\n",
                           "-fdiagnostics-language=zz_ZZ");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ok\n");
}

TEST(DiagnosticLanguage, VersionSaysWhichCatalogItFound) {
    // The only positive evidence a catalog was found.  Everything else about a
    // missing one looks exactly like not having asked for one.
    std::string Out = runPlang("--version");
    EXPECT_NE(Out.find("Messages: en_US (built-in)"), std::string::npos) << Out;

    std::string Loc = runPlang("-fdiagnostics-language=qps_ploc --version");
    EXPECT_NE(Loc.find("qps_ploc.po"), std::string::npos) << Loc;
}

TEST(DiagnosticLanguage, TheFrontEndAgreesWithTheDriverAboutTheCatalog) {
    // Two processes resolving independently; if they disagreed, half a
    // compilation's messages would be in one language and half in the other.
    std::string D = runPlang("-fdiagnostics-language=qps_ploc --version");
    std::string F = runPC1("-fdiagnostics-language=qps_ploc --version");
    const auto line = [](const std::string &S) {
        const auto P = S.find("Messages: ");
        return P == std::string::npos ? std::string() : S.substr(P);
    };
    EXPECT_FALSE(line(D).empty()) << D;
    EXPECT_EQ(line(D), line(F));
}

// ---------------------------------------------------------------------------
// -std=iso7185 means standard Pascal.
//
// The flag selected the dialect for the required words and for most of the
// syntax, but the rest of Extended Pascal came along regardless: string(n),
// '**', substr, card, halt and their neighbors were all available under
// either standard, so nothing said what was standard and what was not.
// ---------------------------------------------------------------------------

namespace {
/// One Extended Pascal construct, in a program that uses nothing else that
/// standard Pascal lacks — so that the diagnostic under -std=iso7185 is about
/// the construct named and not about something else in the way.
struct EPConstruct { const char* What; const char* Src; };

const EPConstruct kEPConstructs[] = {
    {"string(n)",
     "program p; var s: string(10);\n"
     "begin s := 'abc'; writeln(s) end.\n"},
    {"string",
     "program p; var s: string;\n"
     "begin s := 'abc'; writeln(s) end.\n"},
    {"complex",
     "program p; var c: complex;\n"
     "begin c := cmplx(1, 2); writeln(re(c):0:1) end.\n"},
    {"**",
     "program p; var r: real;\n"
     "begin r := 2 ** 3; writeln(r:0:1) end.\n"},
    {"substr",
     "program p; var s: string(10);\n"
     "begin s := 'abcdef'; writeln(substr(s, 2, 3)) end.\n"},
    {"card",
     "program p; var s: set of 1..5;\n"
     "begin s := [1, 3]; writeln(card(s)) end.\n"},
    {"halt",
     "program p;\n"
     "begin writeln('bye'); halt end.\n"},
    {"length",
     "program p;\n"
     "begin writeln(length('abc')) end.\n"},
    {"index",
     "program p;\n"
     "begin writeln(index('abc', 'b')) end.\n"},
    {"trim",
     "program p; var s: string(10);\n"
     "begin s := 'ab'; writeln(trim(s)) end.\n"},
    {"eq",
     "program p;\n"
     "begin writeln(EQ('a', 'a')) end.\n"},
    {"readstr",
     "program p; var s: string(10); i: integer;\n"
     "begin s := '42'; readstr(s, i); writeln(i) end.\n"},
    {"writestr",
     "program p; var s: string(10);\n"
     "begin writestr(s, 42); writeln(s) end.\n"},
    {"><",
     "program p; var a, b: set of 1..5;\n"
     "begin a := [1, 2]; b := [2, 3]; a := a >< b; writeln(1 in a) end.\n"},
    {"for ... in",
     "program p; var s: set of 1..5; i: integer;\n"
     "begin s := [1, 3]; for i in s do write(i); writeln end.\n"},
    {"case otherwise",
     "program p; var i: integer;\n"
     "begin i := 9; case i of 1: writeln('one'); else writeln('other') end end.\n"},
    {"case range",
     "program p; var i: integer;\n"
     "begin i := 2; case i of 1..3: writeln('low') end end.\n"},
    {"succ(x, n)",
     "program p;\n"
     "begin writeln(succ(1, 2)) end.\n"},
};
} // namespace

TEST(StandardGate, EveryExtensionIsTurnedAwayUnderIso7185) {
    for (const auto& [What, Src] : kEPConstructs) {
        auto R = compileAndRun(Src, "-std=iso7185");
        EXPECT_NE(R.ExitCode, 0) << What << " was accepted";
        EXPECT_NE(R.Stderr.find("Extended Pascal extension"), std::string::npos)
            << What << ": " << R.Stderr;
    }
}

TEST(StandardGate, AndEveryOneOfThemStillCompilesUnderIso10206) {
    for (const auto& [What, Src] : kEPConstructs) {
        auto R = compileAndRun(Src, kEP);
        EXPECT_EQ(R.ExitCode, 0) << What << ": " << R.Stderr;
    }
}

// The gate is on the dialect, not on the flag being written out: the standard
// plang reads by default is the one it says it reads by default.
TEST(StandardGate, IsTheDefaultWithNoFlagAtAll) {
    auto R = compileAndRun(
        "program p; var s: string(10);\n"
        "begin s := 'abc'; writeln(s) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("Extended Pascal extension"), std::string::npos)
        << R.Stderr;
}

// What standard Pascal does have keeps working, including the names that read
// like the gated ones — a set is still a set without card, and a case
// statement is still a case statement without an otherwise part.
TEST(StandardGate, StandardPascalIsUntouched) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..5; i: integer; a: packed array[1..3] of char;\n"
        "begin\n"
        "  s := [1, 3]; i := 0;\n"
        "  if 3 in s then i := succ(i);\n"
        "  case i of 1: write('one'); 2: write('two') end;\n"
        "  a := 'abc'; writeln(' ', a, ' ', round(2.6), ' ', odd(i))\n"
        "end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "one abc 3 true\n");
}

// A name Extended Pascal reserves for itself is a name to declare and use
// under standard Pascal, which is why these are gated where they are used
// rather than left out of the symbol table.
TEST(StandardGate, ANameOfOneOfThemMayBeDeclaredUnderIso7185) {
    auto R = compileAndRun(
        "program p;\n"
        "var card, length: integer;\n"
        "function trim(x: integer): integer; begin trim := x - 1 end;\n"
        "begin card := 4; length := trim(card); writeln(card, ' ', length) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 3\n");
}

// ISO §6.7.2: a component of a function result is read from the result, which
// used to be a syntax error at the '.' — so binding(f) could only be used by
// assigning it to a variable first.
TEST(FunctionResult, AFieldOfTheResultIsSelectable) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  b.name := '/tmp/plang_result_field.txt';\n"
        "  bind(f, b); rewrite(f);\n"
        "  writeln(binding(f).bound, ' ', binding(f).name)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true /tmp/plang_result_field.txt\n");
}

TEST(FunctionResult, AParameterlessFunctionAnswersTheSameWay) {
    auto R = compileAndRun(
        "program p;\n"
        "type pt = record x, y: integer end;\n"
        "function origin: pt;\n"
        "var t: pt;\n"
        "begin t.x := 3; t.y := 4; origin := t end;\n"
        "begin writeln(origin.x + origin.y, ' ', origin().x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 3\n");
}

TEST(FunctionResult, AnElementAndAPointeeAreReachableToo) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "function ramp: row;\n"
        "var t: row; i: integer;\n"
        "begin for i := 1 to 3 do t[i] := i * i; ramp := t end;\n"
        "function head: ^integer;\n"
        "var q: ^integer;\n"
        "begin new(q); q^ := 9; head := q end;\n"
        "begin writeln(ramp[3], ' ', head^) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 9\n");
}

// ---------------------------------------------------------------------------
// Warnings
//
// Each of these reports something the standard permits a processor to pass
// over: an error §5.1 f) 1) allows to go unreported, or a construct that is
// well formed and cannot have been meant.  None of them rejects a program, so
// every case here compiles and runs; what is asserted is what was said along
// the way.
// ---------------------------------------------------------------------------

TEST(Warnings, EachOneHasAName) {
    // A warning that cannot be turned off individually is one a project with a
    // house style has to turn off wholesale, so the name is part of the
    // feature and not decoration.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("before it has been given a value"), std::string::npos)
        << R.Stderr;

    auto Off = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin writeln(i) end.\n", "-Wno-var-uninitialized");
    ASSERT_EQ(Off.ExitCode, 0) << Off.Stderr;
    EXPECT_EQ(Off.Stderr.find("before it has been given a value"), std::string::npos)
        << "-Wno- did not silence it:\n" << Off.Stderr;
}

TEST(Warnings, MinusWSilencesAndWerrorPromotes) {
    const char* Src =
        "program p(output);\n"
        "var i: integer;\n"
        "begin writeln(i) end.\n";
    auto Quiet = compileAndRun(Src, "-w");
    ASSERT_EQ(Quiet.ExitCode, 0) << Quiet.Stderr;
    EXPECT_EQ(Quiet.Stderr.find("warning"), std::string::npos) << Quiet.Stderr;

    auto Fatal = compileAndRun(Src, "-Werror");
    EXPECT_NE(Fatal.ExitCode, 0) << "-Werror did not reject:\n" << Fatal.Stderr;
    EXPECT_NE(Fatal.Stderr.find("error"), std::string::npos) << Fatal.Stderr;
}

TEST(Warnings, AnUnknownNameIsReportedRatherThanIgnored) {
    auto R = compileAndRun(
        "program p(output);\nbegin writeln(1) end.\n", "-Wno-no-such-warning");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("unknown warning"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ReadingAVariableBeforeAssigningIt) {
    // 6.5.1: the variable has whatever value the storage held, and 5.1 f) 1)
    // lets that go unreported.  Where the walk can see it, it is said.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i, j: integer;\n"
        "begin j := 0; if j = 0 then i := 1; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'i' is read here before"), std::string::npos) << R.Stderr;
}

// EP §6.4.1: a var declaration's own 'value' clause gives the variable a
// value before the walk sees a single statement -- the same as a parameter
// or a for-loop's control variable does implicitly, both of which the walk
// already knows about. This one wasn't seeded into the initial FlowState, so
// reading x before any explicit assignment statement was reported as though
// `value 5` had done nothing at all.
TEST(Warnings, AVarLevelValueClauseCountsAsAnAssignment) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var x: integer value 5;\n"
        "begin writeln(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("read here before"), std::string::npos) << R.Stderr;
}

// Same rule, but the 'value' clause is on the TYPE (`type t = integer value
// 5;`) rather than the var declaration itself. This carries the initial
// state on a different TypeNode than the one x's own declaration resolves
// to, reached only by following Sema's Denotes chain -- exactly what
// CodeGen's writtenInitialState already has to do to lower the value
// correctly, but the flow walk did not.
TEST(Warnings, ATypeLevelValueClauseCountsAsAnAssignment) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = integer value 5;\n"
        "var x: t;\n"
        "begin writeln(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("read here before"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AssignmentOnEveryPathIsQuiet) {
    // The walk is a definite-assignment analysis, so a variable assigned in
    // both arms of an if is assigned after it.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i, j: integer;\n"
        "begin j := 0; if j = 0 then i := 1 else i := 2; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("before it has been given"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ABlockUsingLabelsIsLeftAlone) {
    // A goto can land on any label in scope, so "the paths reaching this
    // statement" stops being answerable and the walk declines to guess.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 1;\n"
        "var i: integer;\n"
        "begin goto 1; 1: i := 2; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("before it has been given"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ProceduresThatGiveAVariableAValue) {
    // read and readln assign what they are given, and new assigns the pointer.
    // The standard procedures carry no parameter list, so each has to be known
    // by name or its argument reads as a use of an unset variable.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "type pi = ^integer;\n"
        "var i: integer; q: pi;\n"
        "begin read(i); new(q); q^ := i; writeln(q^); dispose(q) end.\n",
        "", "7\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("before it has been given"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AVarParameterIsWhereTheCalleePutsItsAnswer) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure q(var x: integer); begin x := 1 end;\n"
        "begin q(i); writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("before it has been given"), std::string::npos) << R.Stderr;
}

TEST(Warnings, TheControlVariableIsUndefinedAfterItsFor) {
    // 6.8.3.9 says so in as many words, and says it of a variable that plainly
    // did have a value a moment earlier, so the message is its own.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do writeln(i); writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("leaves its control variable undefined"), std::string::npos)
        << R.Stderr;
}

TEST(Warnings, AssigningTheControlVariableAfterTheLoopSettlesIt) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do writeln(i); i := 0; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("control variable undefined"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AFunctionResultLeftUnsetOnSomePath) {
    // 6.6.2: the value of a function is the last one assigned to its result.
    // Assigning it somewhere is already required; assigning it everywhere is
    // what a caller actually depends on.
    auto R = compileAndRun(
        "program p(output);\n"
        "function f(x: integer): integer;\n"
        "begin if x > 0 then f := 1 end;\n"
        "begin writeln(f(-1)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("does not assign to its result on every path"),
              std::string::npos) << R.Stderr;
}

TEST(Warnings, AResultSetInEveryCaseArmIsQuiet) {
    // 6.8.3.5 makes a selector matching no arm an error reported at run time,
    // so every path that carries on past the case went through an arm.
    auto R = compileAndRun(
        "program p(output);\n"
        "function f(x: 1..2): integer;\n"
        "begin case x of 1: f := 10; 2: f := 20 end end;\n"
        "begin writeln(f(1)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("every path"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AResultAssignedByANestedFunctionIsNotSecondGuessed) {
    // 6.8.2.2 lets a function nested inside this one assign the result, and
    // the walk does not go in there.  That there is an assignment at all is
    // still checked, on the frame, which does see it.
    auto R = compileAndRun(
        "program p(output);\n"
        "function outer: integer;\n"
        "var t: integer;\n"
        "  function inner: integer;\n"
        "  begin outer := 42; inner := 0 end;\n"
        "begin t := inner end;\n"
        "begin writeln(outer) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("every path"), std::string::npos) << R.Stderr;
}

TEST(Warnings, DivisionByAConstantZero) {
    // 6.7.2.2 makes it an error, reported when the program runs.  With a
    // constant divisor the trap is certain wherever the statement is reached,
    // which is worth saying earlier — but not worth rejecting the program
    // over, since a statement nothing reaches commits no error.
    for (const char* Op : {"div", "mod"}) {
        auto R = compileAndRun(
            std::string("program p(output);\n"
                        "var i: integer;\n"
                        "begin i := 0; if i <> 0 then writeln(1 ") + Op + " 0) end.\n");
        ASSERT_EQ(R.ExitCode, 0) << Op << "\n" << R.Stderr;
        EXPECT_NE(R.Stderr.find("by a constant zero"), std::string::npos)
            << Op << "\n" << R.Stderr;
    }
}

TEST(Warnings, AConstantOutsideTheSubrangeItIsAssignedTo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: 1..10;\n"
        "begin i := 1; if i = 0 then i := 99; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("outside the range 1..10"), std::string::npos) << R.Stderr;
}

// The warning names the range once.  A subrange type's name is its own bounds
// written again — and written as the ordinals underneath, so a char subrange
// called itself 'char 97..122' beside the 'a'..'z' the same warning had just
// spelled properly.
TEST(Warnings, TheOutOfRangeWarningNamesTheRangeOnce) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: 'a'..'z';\n"
        "begin c := 'a'; if c = 'b' then c := 'Q'; writeln(c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'Q' is outside the range 'a'..'z'"),
              std::string::npos)
        << R.Stderr;
    EXPECT_EQ(R.Stderr.find("97"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("of type"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.7.2.5 with §6.4.5: the operands of a relational operator have to be
// compatible — the same type, one a subrange of the other, or both subranges
// of the one host type.  Matching type *kinds* was asked for instead, which
// let two unrelated enumerations be compared and refused a subrange against
// the type it was cut from, Subrange and Enum being different kinds.
// ---------------------------------------------------------------------------

TEST(SubrangeCompare, AnEnumSubrangeComparesWithItsHostType) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type day = (mon, tue, wed, thu, fri, sat, sun);\n"
        "     weekday = mon..fri;\n"
        "var c: weekday; d: day;\n"
        "begin c := wed; d := wed; writeln(c = d);\n"
        "      d := thu; writeln(c < d, ' ', c >= d) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\ntrue false\n");
}

TEST(SubrangeCompare, ACharSubrangeComparesWithChar) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type letter = 'a'..'z';\n"
        "var b: letter; h: char;\n"
        "begin b := 'm'; h := 'm'; writeln(b = h);\n"
        "      h := 'z'; writeln(b < h, ' ', b > h) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\ntrue false\n");
}

// The widths are furthest apart here: a subrange is lowered to i64 and boolean
// to i1, so this is the case the operand widening has to get right.
TEST(SubrangeCompare, ABooleanSubrangeComparesWithBoolean) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type tb = false..true;\n"
        "var x: tb; b: boolean;\n"
        "begin x := true; b := true; writeln(x = b);\n"
        "      b := false; writeln(x = b, ' ', x > b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\nfalse true\n");
}

TEST(SubrangeCompare, TwoSubrangesOfTheOneHostStillCompare) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type day = (mon, tue, wed, thu, fri, sat, sun);\n"
        "var c: mon..fri; e: sat..sun;\n"
        "begin c := mon; e := sat; writeln(c < e) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(SubrangeCompare, TwoDistinctEnumerationsAreNotComparable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type day = (mon, tue, wed);\n"
        "     color = (red, green, blue);\n"
        "var d: day; k: color;\n"
        "begin d := mon; k := red; writeln(d = k) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot compare"), std::string::npos) << R.Stderr;
}

TEST(SubrangeCompare, SubrangesOfUnrelatedHostsAreNotComparable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type day = (mon, tue, wed);\n"
        "var c: mon..tue; b: 'a'..'z';\n"
        "begin c := mon; b := 'q'; writeln(c = b) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot compare"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AStatementNoPathReaches) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 9;\n"
        "begin goto 9; writeln('dead'); 9: writeln('live') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "live\n");
    EXPECT_NE(R.Stderr.find("cannot be reached"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ALabelPutsAStatementBackWithinReach) {
    // The statement after a goto carries a label of its own, so any goto
    // naming it can land there and it is not dead at all.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 9;\n"
        "begin goto 9; 9: writeln('live') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("cannot be reached"), std::string::npos) << R.Stderr;
}

TEST(Warnings, OnlyOneArmOfAnIfLeavingIsNotEnough) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 9;\n"
        "var i: integer;\n"
        "begin i := 1; if i = 1 then goto 9; writeln('live'); 9: writeln('end') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("cannot be reached"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AComparisonTheOperandsRangeHasAlreadySettled) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: 1..10;\n"
        "begin i := 1; if i > 99 then writeln('never') else writeln('always') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("comparison is always false"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("whose range is 1..10"), std::string::npos) << R.Stderr;
}

TEST(Warnings, TheEnumerationBoundsAreNamedNotNumbered) {
    // 97 is not what the program said, and neither is 2.
    auto R = compileAndRun(
        "program p(output);\n"
        "type c = (red, green, blue);\n"
        "var x: c;\n"
        "begin x := red; if x > blue then writeln('never') else writeln('always') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("range is red..blue"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AnImplicitRangeIsNotEvidenceOfAMistake) {
    // Every char lies within chr(0)..chr(255) and every boolean within
    // false..true whether the author thought about it or not, so a range the
    // program did not write is no reason to say anything.
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: char; b: boolean;\n"
        "begin c := 'x'; b := false;\n"
        "  if (c > 'a') and (b = false) then writeln('ok') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("comparison is always"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ACaseThatCannotCoverItsSelector) {
    // 6.8.3.5: reaching it with an unmatched value is an error.  Where the
    // selector's type can be enumerated, the values that would do it can be
    // named before the program runs.
    auto R = compileAndRun(
        "program p(output);\n"
        "type c = (red, green, blue);\n"
        "var x: c;\n"
        "begin x := red; case x of red: writeln(1); green: writeln(2) end end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("does not cover blue"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AnExhaustiveCaseIsQuiet) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type c = (red, green);\n"
        "var x: c;\n"
        "begin x := red; case x of red: writeln(1); green: writeln(2) end end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("does not cover"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AnOtherwisePartAnswersForEveryValueTheArmsMiss) {
    // EP §6.9.3.5, and `case i of 1: f otherwise end` is the idiom for "and
    // nothing for the rest" — so it is the part being written that settles
    // this, not the part having a statement in it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type c = (red, green, blue);\n"
        "var x: c;\n"
        "begin x := red; case x of red: writeln(1); otherwise end end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("does not cover"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ACharSelectorIsNotHeldToAll256) {
    // The range is the type's, not the program's; demanding the other 230
    // values would be a warning nobody would keep switched on.
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: char;\n"
        "begin c := 'a'; case c of 'a': writeln(1); 'b': writeln(2) end end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("does not cover"), std::string::npos) << R.Stderr;
}

TEST(Warnings, ADeclarationNothingMentions) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i, spare: integer;\n"
        "begin i := 1; writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'spare' is declared but never used"), std::string::npos)
        << R.Stderr;
}

TEST(Warnings, AParameterTheBodyNeverNames) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure q(x, y: integer); begin writeln(x) end;\n"
        "begin q(1, 2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stderr.find("parameter 'y' is never used"), std::string::npos) << R.Stderr;
}

TEST(Warnings, DrivingALoopCountsAsUsingTheVariable) {
    // The control variable is named by the for-statement itself rather than in
    // an expression, so nothing else marks it.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do writeln('x') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("never used"), std::string::npos) << R.Stderr;
}

// EP §6.4.9: `type of x` names x directly in a type-denoter rather than in an
// expression, same as a for-statement's control variable above -- and the
// same gap applied: nothing marked x referenced, so a variable used only to
// give another one its type was wrongly flagged unused.
TEST(Warnings, TypeOfCountsAsUsingTheVariable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var x: integer;\n"
        "var y: type of x;\n"
        "begin y := 5; writeln(y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("never used"), std::string::npos) << R.Stderr;
}

TEST(Warnings, AProgramThatDoesNotTypecheckGetsNoFlowWarnings) {
    // The walk would be reading a tree where some names have no type and some
    // statements were kept only to carry on looking for errors.  Whatever it
    // said about that would be guesswork stacked on a mistake the reader
    // already has to fix.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin writeln(i); writeln(nosuchname) end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("undefined identifier"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("before it has been given"), std::string::npos)
        << "flow warning from a program that does not typecheck:\n" << R.Stderr;
}

// ---------------------------------------------------------------------------
// Program parameters and when the standard input is first touched
// ---------------------------------------------------------------------------

TEST(ProgramParameters, DeclaringInputDoesNotWaitForItBeforeWriting) {
    // §6.10 has the program parameter `input` reset as the program starts, and
    // §6.5.5 has reset fill the buffer variable — which means reading a
    // character, and on a terminal, waiting for one to be typed.  So
    //
    //     program count(input, output);
    //     var i: integer;
    //     begin for i := 1 to 10 do writeln(i) end.
    //
    // printed nothing and sat there, having asked for a keystroke with no way
    // to say so: the prompt it never wrote was still in the output buffer.
    //
    // The window is filled on first use instead.  Nothing is lost by waiting,
    // because priming pushes the character back and leaves the position where
    // it was, so an unprimed stream and a primed one are in the same place.
    const std::string Dir = makeTempDir();
    const std::string Src = Dir + "/case.pas";
    writeFileAt(Src,
        "program count(input, output);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n");

    char Bin[] = "/tmp/plang_regtest_XXXXXX";
    close(mkstemp(Bin));
    const int Rc = std::system((std::string(PLANG_PATH) + " " + Src
                                + " -o " + Bin + " 2>/dev/null").c_str());
    ASSERT_EQ(Rc, 0) << "compile failed";

    const std::string Out = runWithStdinHeldOpen(Bin);
    std::remove(Bin);
    removeTempDir(Dir);

    EXPECT_NE(Out.find("exit:0"), std::string::npos)
        << "did not finish with the standard input still open; 137 is the "
           "watchdog, meaning it is waiting for input it never asked for:\n"
        << Out;
    EXPECT_NE(Out.find("1\n2\n3\n"), std::string::npos) << Out;
}

TEST(ProgramParameters, AProgramThatDoesReadStillGetsEveryCharacter) {
    // The other half of the same change: putting the read off must not lose
    // the first character, which is what would happen if the window were
    // filled by consuming rather than by peeking.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var a, b, c: char;\n"
        "begin read(a); read(b); read(c); writeln(a, b, c) end.\n",
        "", "xyz\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "xyz\n");
}

TEST(ProgramParameters, EofOnAnEmptyStandardInputIsStillTrue) {
    // eof is one of the operations that has to look at the window, so it is
    // one of the places that now fills it.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "begin if eof then writeln('empty') else writeln('not') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "empty\n");
}

TEST(ProgramParameters, EofIsFalseWhenThereIsSomethingToRead) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char;\n"
        "begin if eof then writeln('empty') else begin read(c); writeln(c) end end.\n",
        "", "q\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "q\n");
}

// ---------------------------------------------------------------------------
// Diagnostic rendering: the source line and the caret
//
// A diagnostic used to be one line, "file:line:col: severity: message", since
// the compiler no longer had the source by the time it printed one.  It does
// now: a SourceManager owns every buffer, and a SourceLocation is an offset
// into it rather than a filename and a line number copied into every token.
// ---------------------------------------------------------------------------

TEST(CaretDiagnostics, QuotesTheOffendingLine) {
    auto R = compileAndRun("program p;\n"
                           "var x: integer;\n"
                           "begin x := notdeclared end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("begin x := notdeclared end."), std::string::npos)
        << R.Stderr;
}

TEST(CaretDiagnostics, PutsTheCaretUnderTheColumnReported) {
    auto R = compileAndRun("program p;\n"
                           "var x: integer;\n"
                           "begin x := notdeclared end.\n");
    ASSERT_NE(R.ExitCode, 0);
    // The caret line carries nothing but blanks and the caret, and the caret
    // stands in the column the headline named.
    const size_t Col = R.Stderr.find(":3:");
    ASSERT_NE(Col, std::string::npos) << R.Stderr;
    const unsigned Reported =
        static_cast<unsigned>(std::stoul(R.Stderr.substr(Col + 3)));
    size_t CaretLine = R.Stderr.find("\n^");
    if (CaretLine == std::string::npos) CaretLine = R.Stderr.find("\n ");
    ASSERT_NE(CaretLine, std::string::npos) << R.Stderr;
    const size_t Caret = R.Stderr.find('^', CaretLine);
    ASSERT_NE(Caret, std::string::npos) << R.Stderr;
    EXPECT_EQ(Caret - CaretLine, Reported) << R.Stderr;
}

TEST(CaretDiagnostics, IndentsTheCaretWithTheLinesOwnWhitespace) {
    auto R = compileAndRun("program p;\n"
                           "var x: integer;\n"
                           "begin\n"
                           "        x := notdeclared\n"
                           "end.\n");
    ASSERT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("\n             ^"), std::string::npos) << R.Stderr;
}

TEST(CaretDiagnostics, AnErrorWithNoPlaceInTheSourcePrintsNoSnippet) {
    // "no such file" has nowhere to point, so there is no line to quote.
    std::string Out = runPC1("/nonexistent_file_plang_test.pas");
    EXPECT_EQ(Out.find('^'), std::string::npos) << Out;
    EXPECT_EQ(diagCount(Out), 1) << Out;
}

// ---------------------------------------------------------------------------
// -ferror-limit
// ---------------------------------------------------------------------------

TEST(ErrorLimit, StopsReportingAfterTheGivenNumber) {
    const std::string Src = "program p;\nbegin a:=1; b:=2; c:=3; d:=4 end.\n";
    auto Unlimited = compileAndRun(Src);
    auto Limited   = compileAndRun(Src, "-ferror-limit=2");
    EXPECT_GT(diagCount(Unlimited.Stderr), 2) << Unlimited.Stderr;
    EXPECT_EQ(diagCount(Limited.Stderr), 2)   << Limited.Stderr;
}

TEST(ErrorLimit, ZeroMeansNoLimit) {
    const std::string Src = "program p;\nbegin a:=1; b:=2; c:=3; d:=4 end.\n";
    auto Unlimited = compileAndRun(Src);
    auto Explicit  = compileAndRun(Src, "-ferror-limit=0");
    EXPECT_EQ(diagCount(Explicit.Stderr), diagCount(Unlimited.Stderr))
        << Explicit.Stderr;
}

TEST(ErrorLimit, TheProgramStillFails) {
    auto R = compileAndRun("program p;\nbegin a:=1; b:=2; c:=3 end.\n",
                           "-ferror-limit=1");
    EXPECT_NE(R.ExitCode, 0) << R.Stderr;
}

// ---------------------------------------------------------------------------
// The driver and the front end share one list of options
//
// They used to keep separate ones, and an option added to the front end alone
// was called "unrecognized" by the driver and dropped instead of forwarded.
// ---------------------------------------------------------------------------

TEST(OptionTable, DriverForwardsAFrontEndOptionItHasNoUseFor) {
    auto R = compileAndRun("program p;\nbegin a:=1; b:=2; c:=3 end.\n",
                           "-ferror-limit=1");
    EXPECT_EQ(R.Stderr.find("unrecognized argument"), std::string::npos)
        << R.Stderr;
    EXPECT_EQ(diagCount(R.Stderr), 1) << R.Stderr;
}

TEST(OptionTable, AnOptionNobodyKnowsIsStillReported) {
    std::string Out = runPlang("-fnot-a-real-option /dev/null");
    EXPECT_NE(Out.find("unrecognized argument"), std::string::npos) << Out;
}

TEST(OptionTable, BothHelpTextsListTheWarningOptions) {
    // usagePC1 used to omit -Wno-, -Wall and -I although the front end took
    // all three.  Both texts are rendered from the shared table now.
    std::string Driver   = runPlang("--help");
    std::string Frontend = runPC1("--help");
    for (const char *Opt : {"-Wno-<warning>", "-Wall", "-I<dir>"}) {
        EXPECT_NE(Driver.find(Opt), std::string::npos)   << Opt << "\n" << Driver;
        EXPECT_NE(Frontend.find(Opt), std::string::npos) << Opt << "\n" << Frontend;
    }
}

TEST(OptionTable, TheFrontEndHelpOmitsWhatOnlyTheDriverDoes) {
    std::string Frontend = runPC1("--help");
    EXPECT_EQ(Frontend.find("-Xlinker"), std::string::npos) << Frontend;
    EXPECT_EQ(Frontend.find("-save-temps"), std::string::npos) << Frontend;
}

// ---------------------------------------------------------------------------
// Driver diagnostics
//
// The driver used to print its errors straight to stderr with its own
// coloring, which put them outside everything -w, -Werror, -Wno-<name> and
// -ferror-limit decide.  They go through the same DiagnosticsEngine as the
// rest now, and are cataloged in DiagnosticDriverKinds.def.
// ---------------------------------------------------------------------------

TEST(DriverDiagnostics, ADriverErrorIsPrintedUnderTheProgramName) {
    // It has no source location to name, having nothing to do with a source
    // file, so the printer puts the program name where the file would go.
    auto [Rc, Out] = runPlangRc("");
    EXPECT_EQ(Rc, 1);
    EXPECT_EQ(Out, "plang: error: no input files\n");
}

TEST(DriverDiagnostics, TheDriverAndTheFrontEndAgreeOnAMissingFile) {
    // One condition, one message: the driver reports the scanner's
    // err_file_not_found rather than keeping a copy of the wording.  The
    // prefix is the only difference, and clang -cc1 differs from clang the
    // same way.
    std::string Driver   = runPlang("nosuchfile.pas");
    std::string Frontend = runPC1("nosuchfile.pas");
    EXPECT_EQ(Driver,   "plang: error: no such file or directory: 'nosuchfile.pas'\n");
    EXPECT_EQ(Frontend, "error: no such file or directory: 'nosuchfile.pas'\n");
}

TEST(DriverDiagnostics, AnUnrecognizedArgumentAnswersToItsOwnName) {
    // Cataloged as a warning, so it has a -W name derived from the DiagID
    // like every other warning, and can be turned off on its own.
    std::string On  = runPlang("-fnot-a-real-option /dev/null");
    std::string Off = runPlang("-fnot-a-real-option -Wno-unrecognized-argument /dev/null");
    EXPECT_NE(On.find("plang: warning: unrecognized argument"), std::string::npos) << On;
    EXPECT_EQ(Off.find("unrecognized argument"), std::string::npos) << Off;
}

TEST(DriverDiagnostics, MinusWSilencesADriverWarning) {
    std::string Out = runPlang("-fnot-a-real-option -w /dev/null");
    EXPECT_EQ(Out.find("unrecognized argument"), std::string::npos) << Out;
}

TEST(DriverDiagnostics, WerrorMakesADriverWarningFatal) {
    // The point of the exercise: -Werror reached only the front end before,
    // so a driver warning stayed a warning and the compilation went ahead.
    auto [Rc, Out] = runPlangRc("-fnot-a-real-option -Werror /dev/null");
    EXPECT_EQ(Rc, 1) << Out;
    EXPECT_NE(Out.find("plang: error: unrecognized argument"), std::string::npos) << Out;
}

TEST(DriverDiagnostics, TheDriverWarningIsListedWithTheRest) {
    std::string Out = runPlang("--help-warnings");
    EXPECT_NE(Out.find("-Wno-unrecognized-argument"), std::string::npos) << Out;
}

TEST(DriverDiagnostics, AnOptionMissingItsValueIsReportedNotAsserted) {
    for (const char *Opt : {"-o", "-I", "-Xlinker"}) {
        auto [Rc, Out] = runPlangRc(Opt);
        EXPECT_EQ(Rc, 1) << Opt << "\n" << Out;
        EXPECT_EQ(Out, std::string("plang: error: ") + Opt +
                           " requires an argument\n") << Out;
    }
}

TEST(DriverDiagnostics, AnUnknownDialectNamesTheOnesThereAre) {
    auto [Rc, Out] = runPlangRc("-std=klingon /dev/null");
    EXPECT_EQ(Rc, 1);
    EXPECT_NE(Out.find("plang: error: unknown Pascal dialect 'klingon'"),
              std::string::npos) << Out;
    EXPECT_NE(Out.find("iso7185"), std::string::npos) << Out;
}

// ---------------------------------------------------------------------------
// Color
//
// Two processes print diagnostics, and both used to decide color for
// themselves by probing stderr, with no way to be told otherwise.
// ---------------------------------------------------------------------------

TEST(ColorDiagnostics, APipeGetsNoEscapeSequences) {
    // popen gives a pipe, not a terminal, so this is the default answer.
    std::string Out = runPlang("nosuchfile.pas");
    EXPECT_EQ(Out.find("\033["), std::string::npos) << Out;
}

TEST(ColorDiagnostics, TheFlagOverridesThePipeForTheDriver) {
    std::string Out = runPlang("-fcolor-diagnostics nosuchfile.pas");
    EXPECT_NE(Out.find("\033[1;31merror\033[0m"), std::string::npos) << Out;
}

TEST(ColorDiagnostics, TheFlagReachesTheFrontEndToo) {
    // -fcolor-diagnostics is Consumer::Both in Options.def, so the driver acts
    // on it and hands it on; this diagnostic comes from the other process.
    const std::string Dir = makeTempDir();
    const std::string Src = Dir + "/c.pas";
    ASSERT_TRUE(writeFileAt(Src, "program p; var x: integer;\nbegin x := true end.\n"));

    std::string Color = runPlang("-fcolor-diagnostics " + Src);
    std::string Plain  = runPlang(Src);
    EXPECT_NE(Color.find("\033[1;31merror\033[0m"), std::string::npos) << Color;
    EXPECT_EQ(Plain.find("\033["), std::string::npos) << Plain;
    removeTempDir(Dir);
}

TEST(ColorDiagnostics, TurningItOffIsAccepted) {
    // Nothing to see on a pipe, but it must not be mistaken for an argument
    // nobody knows, which is what it would have been before it was added.
    std::string Out = runPlang("-fno-color-diagnostics nosuchfile.pas");
    EXPECT_EQ(Out, "plang: error: no such file or directory: 'nosuchfile.pas'\n");
}

// ---------------------------------------------------------------------------
// Codegen can see the dialect
//
// It could not until now: Codegen::Codegen took a LangOptions and copied three
// scalars out of it, keeping no record of which language it was compiling.
// That was survivable while every dialect difference lived in the front end --
// of the thirty-four sites that ask, none were in CodeGen -- but Turbo's
// differences are largely decisions made while generating code.
//
// There is nothing yet that reads it, so this asserts the wiring rather than a
// behaviour: an -emit-llvm run under each dialect must still produce identical
// IR for a program that uses no dialect-specific feature.  When Turbo starts
// reading langOpts, this is the case that says the two were ever equal.
// ---------------------------------------------------------------------------

TEST(DialectWiring, TheSameProgramGivesTheSameIRUnderBothDialects) {
    const std::string Src =
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n";
    auto Iso = compileAndEmitIR(Src);
    auto EP  = compileAndEmitIR(Src, kEP);
    ASSERT_FALSE(Iso.IR.empty()) << Iso.Stderr;
    ASSERT_FALSE(EP.IR.empty())  << EP.Stderr;
    EXPECT_EQ(Iso.IR, EP.IR)
        << "a program using no dialect feature must lower identically";
}

// ---------------------------------------------------------------------------
// The dialect list is one list
//
// The driver and the front end are separate processes and both validate
// -std=.  They each used to hold their own copy of the names, and had already
// drifted: the front end listed them in a different order and called them
// something else.  Both now read Dialects.def.
// ---------------------------------------------------------------------------

TEST(DialectList, BothProcessesRejectAnUnknownDialectTheSameWay) {
    const std::string D = runPlang("-std=nonesuch nosuchfile.pas");
    const std::string F = runPC1("-std=nonesuch nosuchfile.pas");
    for (const char* Name : {"iso7185", "iso10206", "fpc", "delphi", "turbo"}) {
        EXPECT_NE(D.find(Name), std::string::npos) << "driver: " << D;
        EXPECT_NE(F.find(Name), std::string::npos) << "front end: " << F;
    }
}

TEST(DialectList, BothProcessesAgreeOnWhatIsImplemented) {
    const std::string D = runPlang("-std=turbo nosuchfile.pas");
    const std::string F = runPC1("-std=turbo nosuchfile.pas");
    EXPECT_NE(D.find("not yet implemented"), std::string::npos) << D;
    EXPECT_NE(F.find("not yet implemented"), std::string::npos) << F;
    // The same two, in the same order, from the same list.
    EXPECT_NE(D.find("iso7185, iso10206"), std::string::npos) << D;
    EXPECT_NE(F.find("iso7185, iso10206"), std::string::npos) << F;
}

TEST(DialectList, AnUnimplementedDialectIsRefusedRatherThanSilentlyDemoted) {
    // The front end mapped every -std= that was not iso10206 onto ISO 7185, so
    // an unimplemented dialect would have compiled as standard Pascal and said
    // nothing.  It is rejected in both processes; when turbo becomes real the
    // mapping now carries it rather than dropping it.
    for (const char* Std : {"turbo", "delphi", "fpc"}) {
        const std::string F = runPC1(std::string("-std=") + Std + " nosuchfile.pas");
        EXPECT_NE(F.find("not yet implemented"), std::string::npos)
            << Std << ": " << F;
    }
}
