/// module_test.cpp — modules and separate compilation
///
/// EP §6.11 and the .pmi interface files that carry it between compilations.
/// These cases need more than one source file, so most of them go through
/// compileTwoFiles: compile a module to an object and an interface, compile a
/// program that imports it, link the two, run the result.  What that is really
/// testing is that the interface written by one compilation says enough for
/// the next one to typecheck against, which is a thing only two compilations
/// can show.

#include "DriverHarness.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// EP §6.11 — Tier 13: Modules (driver/codegen tests)
// ---------------------------------------------------------------------------

// Item 72: Module initialization: to begin do stmt; executes before program body
TEST(EP13Modules, ModuleInitializationRuns) {
    auto R = compileAndRun(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := x end;\n"
        "  to begin do writeln('init');\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin\n"
        "  writeln('body')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // 'init' must appear before 'body'
    EXPECT_NE(R.Stdout.find("init"), std::string::npos);
    EXPECT_NE(R.Stdout.find("body"), std::string::npos);
    EXPECT_LT(R.Stdout.find("init"), R.Stdout.find("body"));
}

// Item 72: Module finalization: to end do stmt; executes after program body
TEST(EP13Modules, ModuleFinalizationRuns) {
    auto R = compileAndRun(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := x end;\n"
        "  to end do writeln('done');\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin\n"
        "  writeln('body')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // 'done' must appear after 'body'
    EXPECT_NE(R.Stdout.find("body"), std::string::npos);
    EXPECT_NE(R.Stdout.find("done"), std::string::npos);
    EXPECT_LT(R.Stdout.find("body"), R.Stdout.find("done"));
}

// Item 74: Driver integration — compile module body + program together, run it
TEST(EP13Modules, ModuleBodyCallableFromProgram) {
    auto R = compileAndRun(
        "module Arith;\n"
        "  function double(x: integer): integer;\n"
        "  begin double := x * 2 end;\n"
        "end.\n"
        "program p;\n"
        "  import Arith;\n"
        "var n: integer;\n"
        "begin\n"
        "  n := double(21);\n"
        "  writeln(n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// ---------------------------------------------------------------------------
// A module body is analyzed like a program block (EP §6.11).
//
// It used to be scanned only for the signatures an importer needs, so nothing
// written inside a module was ever type-checked: an error there reached codegen
// as an internal error, or — worse — compiled to the wrong thing without a word.
// Each of these is the same mistake a program body has always rejected.
// ---------------------------------------------------------------------------

TEST(ModuleBodyChecking, AssignmentOfTheWrongTypeIsRejected) {
    auto R = compileAndRun(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  var s: string(5);\n"
        "  begin s := 42; f := x end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(f(1)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0) << "silently miscompiled; stdout was: " << R.Stdout;
    EXPECT_NE(R.Stderr.find("cannot assign"), std::string::npos) << R.Stderr;
}

TEST(ModuleBodyChecking, UndefinedIdentifierIsADiagnosticNotAnICE) {
    auto R = compileAndRun(
        "module M;\n"
        "  function f(x: integer): integer;\n"
        "  begin f := nosuchvar + x end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(f(1)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("undefined identifier 'nosuchvar'"), std::string::npos)
        << R.Stderr;
    // The point of the fix: Sema catches it, so codegen never sees it.
    EXPECT_EQ(R.Stderr.find("internal error"), std::string::npos) << R.Stderr;
}

TEST(ModuleBodyChecking, WrongArgumentCountIsRejected) {
    auto R = compileAndRun(
        "module M;\n"
        "  function g(a, b: integer): integer; begin g := a + b end;\n"
        "  function f(x: integer): integer; begin f := g(x) end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(f(1)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("expects 2 argument"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("IR verification"), std::string::npos) << R.Stderr;
}

TEST(ModuleBodyChecking, ErrorInToBeginDoIsRejected) {
    auto R = compileAndRun(
        "module M;\n"
        "  var n: integer;\n"
        "  to begin do n := nosuchthing;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(n) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("undefined identifier 'nosuchthing'"), std::string::npos)
        << R.Stderr;
}

TEST(ModuleBodyChecking, DuplicateDeclarationIsRejected) {
    auto R = compileAndRun(
        "module M;\n"
        "  var n: integer;\n"
        "  var n: real;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("duplicate declaration"), std::string::npos) << R.Stderr;
}

// Checking the body must not cost anything that already worked.  A module's
// declarations are now resolved once, by checkBlock, and harvested from its
// scope, so these cover the shapes that harvest has to carry across.

TEST(ModuleBodyChecking, EnumConstantsOfAnExportedTypeAreVisible) {
    // Exporting 'color' without 'red' and 'green' left the type unusable:
    // the importer could declare a variable of it but could not name a value.
    auto R = compileAndRun(
        "module M;\n"
        "  type color = (red, green, blue);\n"
        "  function code(c: color): integer; begin code := ord(c) end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "var c: color;\n"
        "begin c := green; writeln(code(c)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

TEST(ModuleBodyChecking, AModuleMayImportAnotherModule) {
    auto R = compileAndRun(
        "module A;\n"
        "  function one: integer; begin one := 1 end;\n"
        "end.\n"
        "module B;\n"
        "  import A;\n"
        "  function two: integer; begin two := one + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import B;\n"
        "begin writeln(two) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ModuleBodyChecking, WhatAModuleImportsIsNotReExported) {
    // B imports A but does not declare 'one', so a program importing only B
    // must not see it: harvest reads B's own scope, not the enclosing one.
    auto R = compileAndRun(
        "module A;\n"
        "  function one: integer; begin one := 1 end;\n"
        "end.\n"
        "module B;\n"
        "  import A;\n"
        "  function two: integer; begin two := one + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import B;\n"
        "begin writeln(one) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0) << "'one' leaked out of B; stdout: " << R.Stdout;
}

TEST(ModuleBodyChecking, ConstsTypesVarsAndInitializationStillWork) {
    auto R = compileAndRun(
        "module M;\n"
        "  const k = 10;\n"
        "  type v = array[1..3] of integer;\n"
        "  var count: integer;\n"
        "  procedure bump; begin count := count + k end;\n"
        "  to begin do count := 0;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "var x: v;\n"
        "begin bump; bump; x[1] := count; writeln(x[1]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "20\n");
}

TEST(ModuleBodyChecking, ANestedProcedureInsideAModuleStillWorks) {
    auto R = compileAndRun(
        "module M;\n"
        "  function outer(x: integer): integer;\n"
        "    function inner(y: integer): integer; begin inner := y * x end;\n"
        "  begin outer := inner(3) end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln(outer(7)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "21\n");
}

TEST(ModuleBodyChecking, ALabelPlacedInToBeginDoCountsAsPlaced) {
    // The label audit and the initialization statement both belong to the
    // module's scope, so the statement has to be checked first — otherwise a
    // label used there is reported as declared and never placed.
    auto R = compileAndRun(
        "module M;\n"
        "  label 1;\n"
        "  to begin do 1: writeln('init');\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin writeln('body') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "init\nbody\n");
}

TEST(ModuleBodyChecking, ALabelNeverPlacedIsStillReported) {
    auto R = compileAndRun(
        "module M;\n"
        "  label 1;\n"
        "  procedure q; begin writeln('q') end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "begin q end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("never defined as a labeled statement"),
              std::string::npos) << R.Stderr;
}

TEST(ModuleBodyChecking, AValidModuleReportsNoDiagnostics) {
    // Resolution happens once now; a second pass over the declarations would
    // show up here as the same message twice.
    auto R = compileAndRun(
        "module M;\n"
        "  const k = 3;\n"
        "  type pair = record a, b: integer end;\n"
        "  function sum(p: pair): integer; begin sum := p.a + p.b * k end;\n"
        "end.\n"
        "program p;\n"
        "  import M;\n"
        "var q: pair;\n"
        "begin q.a := 1; q.b := 2; writeln(sum(q)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
    EXPECT_EQ(R.Stderr, "") << R.Stderr;
}

// ---------------------------------------------------------------------------
// Separate compilation tests — PMI write/read cycle and multi-file builds
// ---------------------------------------------------------------------------

// Basic PMI round-trip: compile a module with a function, import it.
TEST(SeparateCompilation, PMIRoundtripBasicFunction) {
    auto R = compileTwoFiles(
        // Module source
        "module Arith;\n"
        "function Double(x: integer): integer;\n"
        "begin Double := x * 2 end;\n"
        "end.\n",
        // Program source
        "program p;\n"
        "import Arith;\n"
        "var n: integer;\n"
        "begin\n"
        "  n := Double(21);\n"
        "  writeln(n)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// PMI round-trip: module exports a variable.
TEST(SeparateCompilation, PMIRoundtripExportedVar) {
    auto R = compileTwoFiles(
        // Module source
        "module Counter;\n"
        "var Count: integer;\n"
        "procedure Increment;\n"
        "begin Count := Count + 1 end;\n"
        "end.\n",
        // Program source
        "program p;\n"
        "import Counter;\n"
        "begin\n"
        "  Count := 0;\n"
        "  Increment;\n"
        "  Increment;\n"
        "  writeln(Count)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

// PMI round-trip: module exports a type alias used in program.
TEST(SeparateCompilation, PMIRoundtripExportedType) {
    auto R = compileTwoFiles(
        // Module source
        "module Types;\n"
        "type SmallInt = 0..100;\n"
        "function Clamp(x: integer): SmallInt;\n"
        "begin\n"
        "  if x < 0 then Clamp := 0\n"
        "  else if x > 100 then Clamp := 100\n"
        "  else Clamp := x\n"
        "end;\n"
        "end.\n",
        // Program source
        "program p;\n"
        "import Types;\n"
        "var v: SmallInt;\n"
        "begin\n"
        "  v := Clamp(42);\n"
        "  writeln(v)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// EP §6.11.2: the interface file carries the export-list, so the names an
// importer sees are the same whether the module was compiled with it or apart
// from it — including the ones renaming changed.
TEST(SeparateCompilation, TheInterfaceFileCarriesTheExportList) {
    auto R = compileTwoFiles(
        "module SepRenameMod interface;\n"
        "export SepRenameMod = (Squ => Sq2, protected Calls);\n"
        "var Calls: integer;\n"
        "function Squ(x: integer): integer;\n"
        "end.\n"
        "module SepRenameMod;\n"
        "var Calls: integer;\n"
        "    Hidden: integer;\n"
        "function Squ(x: integer): integer;\n"
        "begin Calls := Calls + 1; Squ := x * x end;\n"
        "to begin do begin Calls := 0; Hidden := 0 end;\n"
        "end.\n",
        "program p;\n"
        "import SepRenameMod (Sq2 => Sqr2);\n"
        "begin writeln(Sqr2(6), ' ', Calls) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "36 1\n");
}

TEST(SeparateCompilation, ProtectedSurvivesTheInterfaceFile) {
    auto R = compileTwoFiles(
        "module SepProtectedMod interface;\n"
        "export SepProtectedMod = (protected Calls);\n"
        "var Calls: integer;\n"
        "end.\n"
        "module SepProtectedMod;\n"
        "var Calls: integer;\n"
        "end.\n",
        "program p;\n"
        "import SepProtectedMod;\n"
        "begin Calls := 3 end.\n",
        "-std=iso10206");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("protected"), std::string::npos);
}

TEST(SeparateCompilation, WhatTheExportListLeavesOutDoesNotTravel) {
    auto R = compileTwoFiles(
        "module SepHiddenMod interface;\n"
        "export SepHiddenMod = (Squ);\n"
        "function Squ(x: integer): integer;\n"
        "end.\n"
        "module SepHiddenMod;\n"
        "var Hidden: integer;\n"
        "function Squ(x: integer): integer; begin Squ := x * x end;\n"
        "end.\n",
        "program p;\n"
        "import SepHiddenMod;\n"
        "begin writeln(Hidden) end.\n",
        "-std=iso10206");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("Hidden"), std::string::npos);
}

// A record type reached through an interface file is laid out from the
// declaration in that file, which has to outlive the load that read it.
TEST(SeparateCompilation, ARecordTypeCrossesTheInterfaceFile) {
    auto R = compileTwoFiles(
        "module Shapes interface;\n"
        "export Shapes = (Point, MakePoint);\n"
        "type Point = record x, y: integer end;\n"
        "function MakePoint(a: integer; b: integer): integer;\n"
        "end.\n"
        "module Shapes;\n"
        "type Point = record x, y: integer end;\n"
        "function MakePoint(a: integer; b: integer): integer;\n"
        "begin MakePoint := a + b end;\n"
        "end.\n",
        "program p;\n"
        "import Shapes;\n"
        "var pt: Point;\n"
        "begin\n"
        "  pt.x := 3; pt.y := 4;\n"
        "  writeln(pt.x + pt.y, ' ', MakePoint(1, 2))\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 3\n");
}

// Multi-file driver mode: plang module.pas program.pas -o binary
TEST(SeparateCompilation, MultiFileDriverMode) {
    const std::string TmpDir   = makeTempDir();
    const std::string ModFile  = TmpDir + "/mod.pas";
    const std::string ProgFile = TmpDir + "/prog.pas";
    ASSERT_TRUE(writeFileAt(ModFile,
        "module Math;\n"
        "function Square(x: integer): integer;\n"
        "begin Square := x * x end;\n"
        "end.\n"));
    ASSERT_TRUE(writeFileAt(ProgFile,
        "program p;\n"
        "import Math;\n"
        "begin writeln(Square(7)) end.\n"));

    char BinTmpl[] = "/tmp/plang_sep_XXXXXX";
    int  BinFd     = mkstemp(BinTmpl);
    close(BinFd);

    char ErrTmpl[] = "/tmp/plang_sep_XXXXXX.err";
    int  ErrFd     = mkstemps(ErrTmpl, 4);
    close(ErrFd);

    // Invoke multi-file mode: program file is the main (last to compile),
    // module file is the extra (compiled first, producing the PMI).
    // Pass the module file as the "extra" (second arg) and program as main (first).
    std::string Cmd = std::string(PLANG_PATH)
        + " -std=iso10206"
        + " -I" + TmpDir
        + " " + ProgFile
        + " " + ModFile
        + " -o " + BinTmpl
        + " 2>" + ErrTmpl;
    int CompileRc = std::system(Cmd.c_str());
    std::string Stderr = runCmd(std::string("cat ") + ErrTmpl);
    std::remove(ErrTmpl);
    removeTempDir(TmpDir);

    ASSERT_EQ(CompileRc, 0) << "multi-file compile failed:\n" << Stderr;

    // Run the binary.
    std::string Output = runCmd(std::string(BinTmpl) + " < /dev/null");
    std::remove(BinTmpl);

    EXPECT_EQ(Output, "49\n");
}

// ===========================================================================
// EP §6.4.9: type of x
// ===========================================================================

// A `type of` denoter has no syntactic lowering, so codegen must take the type
// Sema resolved.  Falling back to an integer silently truncated reals and
// flattened records.
TEST(EPTypeOf, TakesTheInquiredVariablesType) {
    auto R = compileAndRun(
        "program p;\n"
        "type rec = record a, b: integer end;\n"
        "var r: real; c: char; q: rec;\n"
        "    x: type of r; y: type of c; z: type of q;\n"
        "begin\n"
        "  x := 1.5;  writeln(x:0:2);\n"
        "  y := 'k';  writeln(y);\n"
        "  z.a := 7; z.b := 9; writeln(z.a, ' ', z.b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1.50\nk\n7 9\n");
}

// ===========================================================================
// ISO §6.10.3: zero field widths
// ===========================================================================

// EP §6.10.3.1(u) allows TotalWidth 0, but §6.10.3.3 case (b) and §6.10.3.4.2
// make it a minimum for numbers: the value is written without padding.
TEST(WriteWidth, ZeroWidthStillWritesNumbers) {
    auto R = compileAndRun(
        "program p;\n"
        "var x: real; n: integer;\n"
        "begin\n"
        "  x := 3.14159; n := 42;\n"
        "  writeln('[', x:0:2, ']');\n"
        "  writeln('[', n:0, ']');\n"
        "  writeln('[', x:8:2, ']');\n"
        "  writeln('[', n:4, ']')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[3.14]\n[42]\n[    3.14]\n[  42]\n");
}

// §6.10.3.2 and §6.10.3.6 give char and string an exact field width, so zero
// writes nothing; §6.10.3.5 defines Boolean in terms of string.
TEST(WriteWidth, ZeroWidthSuppressesCharStringAndBoolean) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char; b: boolean;\n"
        "begin\n"
        "  c := 'X'; b := true;\n"
        "  writeln('[', c:0, '][', b:0, '][', 'hi':0, ']');\n"
        "  writeln('[', c:3, '][', b:6, '][', 'hi':4, ']')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[][][]\n[  X][  true][  hi]\n");
}

// ===========================================================================
// ISO §6.8.3.10: with on records that are not plain variables
// ===========================================================================

// Neither an indexed nor a dereferenced record has a variable entry of its own.
// Skipping them left the field names to resolve as undeclared globals, which
// failed at link time.
TEST(WithStmt, OpensIndexedAndDereferencedRecords) {
    auto R = compileAndRun(
        "program p;\n"
        "type pt = record x, y: integer end;\n"
        "var a: array[1..3] of pt; q: ^pt;\n"
        "begin\n"
        "  with a[2] do begin x := 11; y := 22 end;\n"
        "  writeln(a[2].x, ' ', a[2].y);\n"
        "  new(q);\n"
        "  with q^ do begin x := 33; y := 44 end;\n"
        "  writeln(q^.x, ' ', q^.y)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 22\n33 44\n");
}

// ===========================================================================
// EP §6.9.7: halt
// ===========================================================================

TEST(Halt, PassesItsArgumentAsTheExitStatus) {
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln('before'); halt(3); writeln('after') end.\n", kEP);
    EXPECT_EQ(R.ExitCode, 3);
    EXPECT_EQ(R.Stdout, "before\n");
}

TEST(Halt, BareHaltExitsZero) {
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln('bare'); halt end.\n", kEP);
    EXPECT_EQ(R.ExitCode, 0);
    EXPECT_EQ(R.Stdout, "bare\n");
}

// ===========================================================================
// ISO §6.7.5.4: pack / unpack
// ===========================================================================

TEST(Transfer, PackCopiesFromTheGivenIndex) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..10] of integer;\n"
        "    z: packed array[1..4] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 10 do a[i] := i * 3;\n"
        "  pack(a, 3, z);\n"
        "  for i := 1 to 4 do write(z[i], ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 12 15 18 \n");
}

TEST(Transfer, UnpackCopiesToTheGivenIndex) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: array[1..10] of integer;\n"
        "    z: packed array[1..4] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 4 do z[i] := i * 5;\n"
        "  for i := 1 to 10 do b[i] := 0;\n"
        "  unpack(z, b, 5);\n"
        "  for i := 1 to 10 do write(b[i], ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 0 0 0 5 10 15 20 0 0 \n");
}

// The unpacked array must hold the whole packed array from index i onwards.
TEST(Transfer, PackRejectsAnIndexLeavingTooFewComponents) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..10] of integer;\n"
        "    z: packed array[1..4] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 10 do a[i] := i;\n"
        "  pack(a, 9, z);\n"
        "  writeln('unreachable')\n"
        "end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("out of bounds"), std::string::npos) << R.Stderr;
}

// ===========================================================================
// EP §6.7.5.5: readstr / writestr
// ===========================================================================

// The worked example in §6.7.5.5: writestr(S, 0.168:5:2, 6:3) yields ' 0.17  6'.
TEST(StringTransfer, WritestrMatchesTheStandardExample) {
    auto R = compileAndRun(
        "program p;\n"
        "var S: string(20);\n"
        "begin writestr(S, 0.168:5:2, 6:3); writeln('[', S, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ 0.17  6]\n");
}

// The worked example in §6.7.5.5 NOTE 2: E := '0.0-4'; readstr(E, R, C, I)
// yields R = 0.0, C = '-', I = 4.
TEST(StringTransfer, ReadstrMatchesTheStandardExample) {
    auto R = compileAndRun(
        "program p;\n"
        "var E: string(20); R: real; C: char; I: integer;\n"
        "begin\n"
        "  E := '0.0-4';\n"
        "  readstr(E, R, C, I);\n"
        "  writeln(R:0:1, ' ', C, ' ', I)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0.0 - 4\n");
}

TEST(StringTransfer, WritestrConcatenatesMixedParameters) {
    auto R = compileAndRun(
        "program p;\n"
        "var S: string(30);\n"
        "begin writestr(S, 'n=', 42, ' ok'); writeln('[', S, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[n=42 ok]\n");
}

// Redirecting output for writestr must not disturb ordinary writes.
TEST(StringTransfer, OrdinaryOutputIsUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var S: string(20);\n"
        "begin\n"
        "  writeln('before');\n"
        "  writestr(S, 'captured');\n"
        "  writeln('after ', S)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "before\nafter captured\n");
}

// ===========================================================================
// ISO §6.4.2.4: signed subrange bounds
// ===========================================================================

TEST(Subrange, AcceptsNegativeBoundsInATypeDeclaration) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -1..10; q = -50..-10;\n"
        "var x: r; y: q;\n"
        "begin x := -1; y := -25; writeln(x, ' ', y) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-1 -25\n");
}

TEST(Subrange, AcceptsNegativeBoundsInExtendedPascal) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -1..10;\n"
        "var x: r;\n"
        "begin x := -1; writeln(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-1\n");
}

// ===========================================================================
// EP §6.11.2: qualified imports
// ===========================================================================

TEST(QualifiedImport, CallsAndReadsThroughTheModuleName) {
    auto R = compileAndRun(
        "module m1;\n"
        "  var counter: integer;\n"
        "  procedure bump; begin counter := counter + 1 end;\n"
        "  function fetch: integer; begin fetch := counter end;\n"
        "  to begin do counter := 100;\n"
        "end.\n"
        "program p;\n"
        "  import m1 qualified;\n"
        "begin\n"
        "  m1.bump; m1.bump;\n"
        "  writeln(m1.fetch(), ' ', m1.counter)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "102 102\n");
}

// A qualified import must not make the bare name visible.
TEST(QualifiedImport, DoesNotInjectTheUnqualifiedName) {
    auto R = compileAndRun(
        "module m1;\n"
        "  function fetch: integer; begin fetch := 5 end;\n"
        "end.\n"
        "program p;\n"
        "  import m1 qualified;\n"
        "begin writeln(fetch()) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
}

// ===========================================================================
// EP §6.11.2 / §6.11.3: export lists, and renaming on export and on import
// ===========================================================================

// The export-list is the module's interface: what it names leaves the module,
// and nothing else does.
TEST(ExportList, NamesWhatLeavesTheModule) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (visible);\n"
        "  function visible: integer;\n"
        "end.\n"
        "module m;\n"
        "  var hidden: integer;\n"
        "  function visible: integer; begin visible := 7 end;\n"
        "  procedure secret; begin hidden := 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin writeln(visible()) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(ExportList, WhatItLeavesOutIsUnreachable) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (visible);\n"
        "  function visible: integer;\n"
        "end.\n"
        "module m;\n"
        "  var hidden: integer;\n"
        "  function visible: integer; begin visible := 7 end;\n"
        "  procedure secret; begin hidden := 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin secret; writeln(hidden) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("secret"), std::string::npos);
    EXPECT_NE(R.Stderr.find("hidden"), std::string::npos);
}

// A module with no export-list keeps exporting everything, which is what a
// module written without an interface has always done.
TEST(ExportList, AbsentMeansEverything) {
    auto R = compileAndRun(
        "module m;\n"
        "  var n: integer;\n"
        "  procedure bump; begin n := n + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin n := 0; bump; writeln(n) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

TEST(ExportRename, CallersSeeTheNewName) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (internalSquare => square);\n"
        "  function internalSquare(x: integer): integer;\n"
        "end.\n"
        "module m;\n"
        "  function internalSquare(x: integer): integer;\n"
        "  begin internalSquare := x * x end;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin writeln(square(9)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "81\n");
}

TEST(ExportRename, TheOldNameNoLongerReaches) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (internalSquare => square);\n"
        "  function internalSquare(x: integer): integer;\n"
        "end.\n"
        "module m;\n"
        "  function internalSquare(x: integer): integer;\n"
        "  begin internalSquare := x * x end;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin writeln(internalSquare(9)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
}

// EP §6.11.2: an export-range covers the constants of one enumerated type
// between the two it names.
TEST(ExportList, ARangeCoversTheConstantsBetweenItsEnds) {
    auto R = compileAndRun(
        "module pal interface;\n"
        "  export pal = (color, red..green);\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "module pal;\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "program p;\n"
        "  import pal;\n"
        "var c: color;\n"
        "begin c := yellow; writeln(ord(c)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ExportList, AConstantOutsideTheRangeStaysIn) {
    auto R = compileAndRun(
        "module pal interface;\n"
        "  export pal = (color, red..green);\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "module pal;\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "program p;\n"
        "  import pal;\n"
        "var c: color;\n"
        "begin c := blue end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("blue"), std::string::npos);
}

// EP §6.11.2: 'protected' leaves the value readable where it is imported and
// the variable unassignable.
TEST(ExportList, ProtectedIsReadableButNotAssignable) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (protected count, bump);\n"
        "  var count: integer;\n"
        "  procedure bump;\n"
        "end.\n"
        "module m;\n"
        "  var count: integer;\n"
        "  procedure bump; begin count := count + 1 end;\n"
        "  to begin do count := 0;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin bump; bump; writeln(count) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ExportList, AssigningToAProtectedNameIsRejected) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (protected count);\n"
        "  var count: integer;\n"
        "end.\n"
        "module m;\n"
        "  var count: integer;\n"
        "end.\n"
        "program p;\n"
        "  import m;\n"
        "begin count := 5 end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("protected"), std::string::npos);
}

// EP §6.11.3: import renaming.  Without 'only' the list says what is renamed
// and everything else still comes in.
TEST(ImportRename, RenamesOneNameAndLeavesTheRest) {
    auto R = compileAndRun(
        "module m;\n"
        "  function f(x: integer): integer; begin f := x + 1 end;\n"
        "  function g(x: integer): integer; begin g := x + 2 end;\n"
        "end.\n"
        "program p;\n"
        "  import m (f => plus1);\n"
        "begin writeln(plus1(10), ' ', g(10)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 12\n");
}

TEST(ImportRename, WithOnlyNothingElseComesIn) {
    auto R = compileAndRun(
        "module m;\n"
        "  function f(x: integer): integer; begin f := x + 1 end;\n"
        "  function g(x: integer): integer; begin g := x + 2 end;\n"
        "end.\n"
        "program p;\n"
        "  import m only (f => plus1);\n"
        "begin writeln(plus1(10), ' ', g(10)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'g'"), std::string::npos);
}

TEST(ImportRename, TheNameItWasImportedUnderNoLongerReaches) {
    auto R = compileAndRun(
        "module m;\n"
        "  function f(x: integer): integer; begin f := x + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m (f => plus1);\n"
        "begin writeln(f(10)) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
}

TEST(ImportRename, AQualifiedImportQualifiesTheNewName) {
    auto R = compileAndRun(
        "module m;\n"
        "  function f(x: integer): integer; begin f := x + 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m qualified (f => g);\n"
        "begin writeln(m.g(41)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// Renaming twice over: the module exports under one name and the program
// imports it under a third.  What the object file holds is the first.
TEST(ImportRename, StacksOnTopOfAnExportRename) {
    auto R = compileAndRun(
        "module m interface;\n"
        "  export m = (declared => exported);\n"
        "  function declared: integer;\n"
        "end.\n"
        "module m;\n"
        "  function declared: integer; begin declared := 3 end;\n"
        "end.\n"
        "program p;\n"
        "  import m (exported => local);\n"
        "begin writeln(local()) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(ImportClause, AnUnknownModuleIsReported) {
    auto R = compileAndRun(
        "program p;\n"
        "  import nowhere;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("no module named 'nowhere'"), std::string::npos);
}

TEST(ImportClause, ANameTheInterfaceDoesNotExportIsReported) {
    auto R = compileAndRun(
        "module m;\n"
        "  function f: integer; begin f := 1 end;\n"
        "end.\n"
        "program p;\n"
        "  import m only (f, nope);\n"
        "begin writeln(f()) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'nope' is not exported"), std::string::npos);
}

// An interface declares the signatures; the older plang spelling, in which the
// export section holds the declarations themselves, still reads.
TEST(ModuleInterface, TheOlderExportSpellingStillReads) {
    auto R = compileAndRun(
        "module v interface;\n"
        "  export function scale(x: integer; k: integer): integer;\n"
        "end.\n"
        "module v;\n"
        "  function scale(x: integer; k: integer): integer;\n"
        "  begin scale := x * k end;\n"
        "end.\n"
        "program p;\n"
        "  import v;\n"
        "begin writeln(scale(2, 3)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

// ===========================================================================
// EP §6.4.3.4 / §6.7.5.6: BindingType.name
// ===========================================================================

TEST(Binding, NameFieldIsReadableAndWritable) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_namefield.txt';\n"
        "  bind(f, b);\n"
        "  b := binding(f);\n"
        "  writeln('[', b.name, ']')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[/tmp/plang_bind_namefield.txt]\n");
}

// ===========================================================================
// String(N) on file variables
// ===========================================================================

TEST(FileIO, WritesAndReadsStringVariables) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; s: string(40);\n"
        "begin\n"
        "  s := 'round trip';\n"
        "  rewrite(f); writeln(f, s); reset(f);\n"
        "  s := 'clobbered';\n"
        "  readln(f, s);\n"
        "  writeln('[', s, ']')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[round trip]\n");
}

// ===========================================================================
// EP §6.4.7 / §6.7.3 / §6.7.5.3: undiscriminated schema types
// ===========================================================================

// The schema name alone is a type-denoter only as a parameter-form or a
// pointer domain-type; a variable has to say which member of the family it is.
TEST(Schema, BareNameIsNotAVariableType) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var v: vec;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("discriminants"), std::string::npos) << R.Stderr;
}

// EP §6.7.3.3 and §6.8.4: the actual parameter's discriminants reach the body.
TEST(Schema, VarParameterTakesTheActualsDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var a: vec(3); b: vec(5); i: integer;\n"
        "procedure show(var v: vec);\n"
        "var j: integer;\n"
        "begin\n"
        "  write(v.n, ':');\n"
        "  for j := 1 to v.n do write(' ', v[j]);\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  for i := 1 to 3 do a[i] := i;\n"
        "  for i := 1 to 5 do b[i] := i * 10;\n"
        "  show(a); show(b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3: 1 2 3\n5: 10 20 30 40 50\n");
}

// EP §6.7.3.2: a value parameter is a variable of its own, so writing through
// it must not reach the actual parameter.
TEST(Schema, ValueParameterIsACopy) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var a: vec(3); i: integer;\n"
        "procedure clobber(v: vec);\n"
        "begin v[1] := 999 end;\n"
        "begin\n"
        "  for i := 1 to 3 do a[i] := i;\n"
        "  clobber(a);\n"
        "  writeln(a[1])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

// EP §6.7.5.3: new(p, d) discriminates the created variable, and the value of
// d need not be known until it runs.
TEST(Schema, NewDiscriminatesThroughAPointerDomain) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var q: ^vec; k, i: integer;\n"
        "begin\n"
        "  k := 4;\n"
        "  new(q, k);\n"
        "  for i := 1 to q^.n do q^[i] := i * i;\n"
        "  write(q^.n, ':');\n"
        "  for i := 1 to q^.n do write(' ', q^[i]);\n"
        "  writeln;\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4: 1 4 9 16\n");
}

// Two allocations from the same schema keep their own discriminants, and a
// schematic parameter passes them on unchanged.
TEST(Schema, DiscriminantsTravelThroughNestedCalls) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var p1, p2: ^vec; i: integer;\n"
        "function sum(var v: vec): integer;\n"
        "var j, s: integer;\n"
        "begin s := 0; for j := 1 to v.n do s := s + v[j]; sum := s end;\n"
        "procedure report(var v: vec);\n"
        "  procedure inner(var w: vec);\n"
        "  begin writeln(w.n, ' ', sum(w)) end;\n"
        "begin inner(v) end;\n"
        "begin\n"
        "  new(p1, 3); new(p2, 5);\n"
        "  for i := 1 to 3 do p1^[i] := 1;\n"
        "  for i := 1 to 5 do p2^[i] := 2;\n"
        "  report(p1^); report(p2^);\n"
        "  dispose(p1); dispose(p2)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 3\n5 10\n");
}

// The bounds of a schematic array are re-derived from the discriminants, so an
// index outside them is caught with the actual parameter's range.
TEST(Schema, IndexIsCheckedAgainstTheRuntimeBounds) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var a: vec(3);\n"
        "procedure poke(var v: vec);\n"
        "begin v[5] := 1 end;\n"
        "begin poke(a) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("out of bounds 1..3"), std::string::npos) << R.Stderr;
}

// A bound may be any expression over the discriminants, and there may be more
// than one of them.
TEST(Schema, BoundsMayBeExpressionsOverSeveralDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type win(lo, hi: integer) = array[lo..2 * hi] of integer;\n"
        "var w: win(-2, 2); i: integer;\n"
        "procedure show(var v: win);\n"
        "var j: integer;\n"
        "begin\n"
        "  write(v.lo, '..', 2 * v.hi, ':');\n"
        "  for j := v.lo to 2 * v.hi do write(' ', v[j]);\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  for i := -2 to 4 do w[i] := i;\n"
        "  show(w)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-2..4: -2 -1 0 1 2 3 4\n");
}

// NOTE 1 to EP §6.4.7: a discriminant that the body never mentions still
// distinguishes the types, and the body keeps its ordinary fixed layout.
TEST(Schema, DiscriminantNeedNotAffectTheLayout) {
    auto R = compileAndRun(
        "program p;\n"
        "type tagged(id: integer) = record count: integer end;\n"
        "var t: tagged(7); q: ^tagged;\n"
        "procedure bump(var r: tagged);\n"
        "begin r.count := r.count + r.id end;\n"
        "begin\n"
        "  t.count := 1; bump(t);\n"
        "  writeln(t.id, ' ', t.count);\n"
        "  new(q, 5); q^.count := 10; bump(q^);\n"
        "  writeln(q^.id, ' ', q^.count);\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 8\n5 15\n");
}

// EP §6.4.4 allows a record-bodied schema as a pointer domain-type.  The body
// is laid out at run time from the discriminants the object carries, because
// there is no one struct for it -- layoutOf specialises per discriminant tuple
// and there is no tuple until new() runs.
TEST(Schema, AVaryingRecordBodyIsLaidOutAtRunTime) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(n: integer) = record len: integer; d: array[1..n] of char end;\n"
        "var q: ^buf; i: integer;\n"
        "begin\n"
        "  new(q, 5); q^.len := 5;\n"
        "  for i := 1 to 5 do q^.d[i] := chr(ord('a') + i - 1);\n"
        "  write(q^.len:1, ' ');\n"
        "  for i := 1 to 5 do write(q^.d[i]);\n"
        "  writeln; dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 abcde\n");
}

TEST(Schema, AFieldAfterTheVaryingOneMovesWithIt) {
    // The case that says the offsets are genuinely computed rather than taken
    // from the probe: `n` sits AFTER a field whose size the discriminant fixes,
    // so its offset differs between the two objects.  A probe layout would put
    // both at 8 and the second write would land on top of the first's data.
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(cap: integer) = record s: string(cap); n: integer end;\n"
        "     pb = ^buf;\n"
        "var a, b: pb;\n"
        "begin\n"
        "  new(a, 4);  a^.s := 'abcd';          a^.n := 11;\n"
        "  new(b, 40); b^.s := 'a much longer string'; b^.n := 22;\n"
        "  writeln('[', a^.s, '] ', a^.n:1, ' len=', length(a^.s):1);\n"
        "  writeln('[', b^.s, '] ', b^.n:1, ' len=', length(b^.s):1);\n"
        "  dispose(a); dispose(b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcd] 11 len=4\n[a much longer string] 22 len=20\n");
}

TEST(Schema, AVariantPartInAVaryingBodyIsLaidOutToo) {
    // §6.4.3.3: the alternatives share one run of storage, so the part is as
    // wide as the widest of them -- a max taken at run time, since an
    // alternative's own size may itself depend on a discriminant.  The variant
    // sits after a field whose size the discriminant fixes, so its offset is
    // computed rather than taken from the probe.
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(n: integer) = record d: array[1..n] of char;\n"
        "       case tag: boolean of true: (x: integer); false: (y: real) end;\n"
        "var q: ^buf; i: integer;\n"
        "begin\n"
        "  new(q, 6);\n"
        "  for i := 1 to 6 do q^.d[i] := chr(ord('a') + i - 1);\n"
        "  q^.tag := true; q^.x := 1234;\n"
        "  write('d='); for i := 1 to 6 do write(q^.d[i]);\n"
        "  writeln(' tag=', q^.tag, ' x=', q^.x:1);\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "d=abcdef tag=true x=1234\n");
}

TEST(Schema, TheVariantPartIsAsWideAsItsWidestAlternative) {
    // The max is what stops the smaller alternative's storage being all that is
    // allocated: `y` is a real and `x` an integer, and writing y through a
    // block sized for x would run past the end of the allocation.
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(n: integer) = record d: array[1..n] of char;\n"
        "       case tag: boolean of true: (x: char); false: (y: real) end;\n"
        "var q: ^buf; canary: integer;\n"
        "begin\n"
        "  canary := 4321;\n"
        "  new(q, 3); q^.d[1] := 'z';\n"
        "  q^.tag := false; q^.y := 2.5;\n"
        "  writeln(q^.d[1], ' ', q^.y:3:1, ' ', canary:1);\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "z 2.5 4321\n");
}

TEST(Schema, ADiscriminantMayFixARangeRatherThanAnExtent) {
    // `record k: 1..n end` was refused, and the message blamed the size: the
    // storage is the host ordinal's width whatever n is, and what the
    // discriminant fixes is the RANGE k is checked against.  So there is
    // nothing to lay out differently and everything to check differently.
    // Sema cannot decide it -- the recorded bounds are the probe's -- so the
    // check is emitted against the value the object carries, and the
    // compile-time warning that folded against the probe stands aside.
    auto R = compileAndRun(
        "program p(output);\n"
        "type box(n: integer) = record k: 1..n; m: integer end;\n"
        "var q: ^box;\n"
        "begin\n"
        "  new(q, 100);\n"
        "  q^.k := 50; q^.m := 7;\n"
        "  writeln('k=', q^.k:1, ' m=', q^.m:1);\n"
        "  q^.k := 200;\n"
        "  writeln('not reached')\n"
        "end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_EQ(R.Stdout, "k=50 m=7\n");
    // The real bound, not the probe's 1..1, and no compile-time warning about
    // a trap that does not happen.
    EXPECT_NE(R.Stderr.find("1..100"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("outside the range"), std::string::npos) << R.Stderr;
}

TEST(Schema, ASubrangeBoundIsCheckedAgainstTheConstantItWasWrittenWith) {
    // The bound `1..n*lim` mixes a discriminant with a constant of the
    // enclosing scope.  It used to be re-emitted as an EXPRESSION at the
    // assignment, which resolved `lim` in the procedure doing the assigning --
    // so an unrelated local of that spelling answered for the constant and the
    // object was range-checked against a bound nobody wrote.  Here the local
    // says 3 and the constant says 100, and 50 is in range only under the
    // constant.
    auto R = compileAndRun(
        "program p(output);\n"
        "const lim = 100;\n"
        "type box(n: integer) = record k: 1..n*lim; m: integer end;\n"
        "var q: ^box;\n"
        "procedure touch;\n"
        "var lim: integer;\n"
        "begin\n"
        "  lim := 3;\n"
        "  q^.k := 50; q^.m := lim\n"
        "end;\n"
        "begin\n"
        "  new(q, 2);\n"
        "  touch;\n"
        "  writeln('k=', q^.k:1, ' m=', q^.m:1)\n"
        "end.\n", kEP);
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "k=50 m=3\n");
}

TEST(Schema, AConformantArrayIsToldTheBoundsTheActualActuallyHas) {
    // Same shape, one lowering along: the bounds handed to a conformant array
    // parameter came from re-emitting the actual's declaration in the CALLER's
    // scope.  A local named like the constant in the bound therefore decided
    // how large the callee believed the array to be -- and the callee indexes
    // it.  The form is arithmetic over the discriminant by index with `hi`
    // already folded where the type was declared, so the caller cannot reach
    // it.
    auto R = compileAndRun(
        "program p(output);\n"
        "const hi = 10;\n"
        "type vec(n: integer) = array[1..n*hi] of integer;\n"
        "var v: ^vec;\n"
        "procedure show(var a: array[lo..h: integer] of integer);\n"
        "begin\n"
        "  writeln('bounds ', lo:1, '..', h:1);\n"
        "  a[h] := 9; writeln('last=', a[h]:1)\n"
        "end;\n"
        "procedure caller;\n"
        "var hi: integer;\n"
        "begin hi := 2; show(v^) end;\n"
        "begin new(v, 3); caller end.\n", kEP);
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bounds 1..30\nlast=9\n");
}

TEST(Schema, AFixedRecordBodyIsStillAUsableDomainType) {
    // The opposite direction: a record body that does not read a discriminant
    // has a fixed layout and is accepted, so the check has not widened into
    // refusing every record-bodied schema.
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(cap: integer) = record n: integer end;\n"
        "var q: ^buf;\n"
        "begin new(q, 4); q^.n := 3; writeln('n=', q^.n:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "n=3\n");
}

TEST(Schema, NewReportsAMissingDiscriminant) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var q: ^vec;\n"
        "begin new(q) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("needs 1 discriminant"), std::string::npos) << R.Stderr;
}

// EP §6.7.3.3: both sides have to be schematic, and from the same schema.
TEST(Schema, ArgumentMustComeFromTheSameSchema) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "     other(n: integer) = array[1..n] of integer;\n"
        "var a: other(3);\n"
        "procedure f(var v: vec);\n"
        "begin v[1] := 1 end;\n"
        "begin f(a) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("must be a 'vec'"), std::string::npos) << R.Stderr;
}

// EP §6.7.3.2: schematic values are assignment-compatible only when they carry
// the same tuple, which for undiscriminated operands is a run-time question.
TEST(Schema, WholeValueAssignmentCopiesTheBody) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var a, b: ^vec; i: integer;\n"
        "begin\n"
        "  new(a, 3); new(b, 3);\n"
        "  for i := 1 to 3 do a^[i] := i * 7;\n"
        "  b^ := a^;\n"
        "  writeln(b^[1], ' ', b^[2], ' ', b^[3]);\n"
        "  dispose(a); dispose(b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 14 21\n");
}

TEST(Schema, WholeValueAssignmentRejectsADifferentTuple) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var a, b: ^vec;\n"
        "begin\n"
        "  new(a, 3); new(b, 5);\n"
        "  b^ := a^;\n"
        "  writeln('unreachable')\n"
        "end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("discriminant n differs"), std::string::npos) << R.Stderr;
}

// ===========================================================================
// ISO §6.1.7 / §6.4.2.4: char and folded constant bounds
// ===========================================================================

// A one-character string is a char constant, so it is an ordinal and may bound
// an array.  Getting this wrong sized the array to nothing.
TEST(Bounds, CharLiteralsBoundAnArray) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array['a'..'e'] of integer; c: char;\n"
        "begin\n"
        "  for c := 'a' to 'e' do a[c] := ord(c) - ord('a');\n"
        "  for c := 'a' to 'e' do write(a[c]);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "01234\n");
}

TEST(Bounds, CharLiteralIndexIsRangeChecked) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array['a'..'c'] of integer;\n"
        "begin a['z'] := 1 end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("out of bounds 97..99"), std::string::npos) << R.Stderr;
}

// EP §6.4.3.3: the capacity of string(N) is a constant-expression, not just a
// literal; folding it wrongly let a string outgrow its declared capacity.
TEST(Bounds, StringCapacityAcceptsANamedConstant) {
    auto R = compileAndRun(
        "program p;\n"
        "const maxlen = 5;\n"
        "var s: string(maxlen);\n"
        "begin\n"
        "  s := 'abcde';\n"
        "  writeln('[', s, ']')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcde]\n");
}

TEST(Bounds, ANamedCapacityIsStillEnforced) {
    // The capacity has to reach codegen as 5, not as the 255 an unresolved one
    // would fall back to, and the only way to see the difference is to overrun.
    auto R = compileAndRun(
        "program p;\n"
        "const maxlen = 5;\n"
        "var s: string(maxlen);\n"
        "begin s := 'abcdefghij' end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not fit a string(5)"), std::string::npos)
        << R.Stderr;
}

// ===========================================================================
// ISO §6.6.3.1: procedural and functional parameters
//
// The representation to keep honest is the pair.  A procedure passed as a
// parameter carries both an entry point and the frame its body reads outer
// variables through, because this compiler builds a callee's static-link frame
// out of names visible at the call site — and the place that finally calls a
// procedural parameter cannot see them.  Several tests below pass a nested
// procedure somewhere its captured variables are long out of scope; those are
// the ones that fail if the frame is ever dropped or rebuilt too late.
// ===========================================================================

TEST(ProcParams, FunctionalParameterCallsWhatItWasGiven) {
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "function neg(x: integer): integer; begin neg := -x end;\n"
        "begin writeln(ap(dbl, 21)); writeln(ap(neg, 21)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n-21\n");
}

TEST(ProcParams, ProceduralParameterRunsForItsSideEffect) {
    auto R = compileAndRun(
        "program p;\n"
        "var total: integer;\n"
        "procedure add(x: integer); begin total := total + x end;\n"
        "procedure thrice(procedure act(x: integer); v: integer);\n"
        "begin act(v); act(v); act(v) end;\n"
        "begin total := 0; thrice(add, 5); writeln(total) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "15\n");
}

TEST(ProcParams, VarParameterPassesThrough) {
    auto R = compileAndRun(
        "program p;\n"
        "var k: integer;\n"
        "procedure bump(var x: integer); begin x := x + 10 end;\n"
        "procedure apply(procedure f(var y: integer); var n: integer);\n"
        "begin f(n) end;\n"
        "begin k := 1; apply(bump, k); writeln(k) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n");
}

TEST(ProcParams, NestedTargetStillSeesItsOuterVariables) {
    // The frame has to be the live one, not a snapshot: base changes between
    // the two calls and the second must see the new value.
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function addbase(x: integer): integer;\n"
        "  begin addbase := x + base end;\n"
        "  function ap(function f(x: integer): integer; v: integer): integer;\n"
        "  begin ap := f(v) end;\n"
        "begin\n"
        "  base := 100; writeln(ap(addbase, 5));\n"
        "  base := 200; writeln(ap(addbase, 5))\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "105\n205\n");
}

TEST(ProcParams, NestedTargetReachesATopLevelReceiver) {
    // ap is top level, so 'base' is nowhere in scope where the call happens.
    // The frame must have traveled with addbase.
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function addbase(x: integer): integer;\n"
        "  begin addbase := x + base end;\n"
        "begin base := 1000; writeln(ap(addbase, 7)) end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1007\n");
}

TEST(ProcParams, NestedAndTopLevelReachTheSameFormal) {
    // One wants a frame and the other does not, so they can only share a
    // formal parameter if both are called through the same shape.
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function addbase(x: integer): integer;\n"
        "  begin addbase := x + base end;\n"
        "begin base := 50; writeln(ap(dbl, 3)); writeln(ap(addbase, 3)) end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n53\n");
}

TEST(ProcParams, DepthThreeNestingKeepsBothFrames) {
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "procedure l1;\n"
        "var a: integer;\n"
        "  procedure l2;\n"
        "  var b: integer;\n"
        "    function deep(x: integer): integer;\n"
        "    begin deep := x + a + b end;\n"
        "  begin b := 20; writeln(ap(deep, 3)) end;\n"
        "begin a := 100; l2 end;\n"
        "begin l1 end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "123\n");
}

TEST(ProcParams, ParameterIsHandedOnwards) {
    // Forwarding must reuse the pair as received: rebuilding the frame here is
    // exactly what cannot be done, since the captured names are not in scope.
    auto R = compileAndRun(
        "program p;\n"
        "function inner(function g(x: integer): integer; v: integer): integer;\n"
        "begin inner := g(v) end;\n"
        "function outerf(function f(x: integer): integer; v: integer): integer;\n"
        "begin outerf := inner(f, v) + 1 end;\n"
        "procedure hold;\n"
        "var bias: integer;\n"
        "  function add(x: integer): integer; begin add := x + bias end;\n"
        "begin bias := 300; writeln(outerf(add, 5)) end;\n"
        "begin hold end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "306\n");
}

TEST(ProcParams, ParameterlessFunctionalParameter) {
    // ISO §6.8.2.2: naming it in an expression is a call, and it has to be
    // recognized as one before it is read as if it were storage.
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f: integer): integer;\n"
        "begin ap := f + 1 end;\n"
        "function seven: integer; begin seven := 7 end;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function get: integer; begin get := base end;\n"
        "begin base := 41; writeln(ap(get)) end;\n"
        "begin writeln(ap(seven)); outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8\n42\n");
}

TEST(ProcParams, ParameterlessProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "var hits: integer;\n"
        "procedure ping; begin hits := hits + 1 end;\n"
        "procedure run(procedure a); begin a; a end;\n"
        "begin hits := 0; run(ping); writeln(hits) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ProcParams, RecursionThroughTheParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "function apply(function f(x: integer): integer; v: integer): integer;\n"
        "begin apply := f(v) end;\n"
        "function fact(n: integer): integer;\n"
        "begin if n <= 1 then fact := 1 else fact := n * apply(fact, n - 1) end;\n"
        "begin writeln(apply(fact, 6)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "720\n");
}

TEST(ProcParams, TwoProceduralParametersAtOnce) {
    auto R = compileAndRun(
        "program p;\n"
        "function combine(function f(x: integer): integer;\n"
        "                 function g(x: integer): integer; v: integer): integer;\n"
        "begin combine := f(v) + g(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "function sq(x: integer): integer; begin sq := x * x end;\n"
        "begin writeln(combine(dbl, sq, 5)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "35\n");
}

TEST(ProcParams, AParameterMayItselfTakeAProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "function twice(function h(x: integer): integer; v: integer): integer;\n"
        "begin twice := h(h(v)) end;\n"
        "function useit(function apply(function q(x: integer): integer;\n"
        "                              v: integer): integer;\n"
        "               function f(x: integer): integer): integer;\n"
        "begin useit := apply(f, 3) end;\n"
        "function inc1(x: integer): integer; begin inc1 := x + 1 end;\n"
        "begin writeln(useit(twice, inc1)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(ProcParams, TrapezoidIntegration) {
    // The motivating example for the feature: one routine over any integrand.
    auto R = compileAndRun(
        "program p;\n"
        "function integrate(function f(x: real): real;\n"
        "                   a, b: real; n: integer): real;\n"
        "var i: integer; h, s: real;\n"
        "begin\n"
        "  h := (b - a) / n; s := (f(a) + f(b)) / 2.0;\n"
        "  for i := 1 to n - 1 do s := s + f(a + i * h);\n"
        "  integrate := s * h\n"
        "end;\n"
        "function sq(x: real): real; begin sq := x * x end;\n"
        "function lin(x: real): real; begin lin := 2.0 * x end;\n"
        "begin\n"
        "  writeln(integrate(sq, 0.0, 3.0, 3000):0:4);\n"
        "  writeln(integrate(lin, 0.0, 4.0, 3000):0:4)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9.0000\n16.0000\n");
}

TEST(ProcParams, StructuredParameterTypesPassThrough) {
    auto R = compileAndRun(
        "program p;\n"
        "type pt = record x, y: integer end;\n"
        "     vec = array[1..3] of integer;\n"
        "var a: pt; v: vec;\n"
        "procedure showpt(q: pt); begin writeln(q.x + q.y) end;\n"
        "procedure showvec(q: vec); begin writeln(q[1] + q[2] + q[3]) end;\n"
        "procedure shows(s: string); begin writeln(s) end;\n"
        "procedure runpt(procedure s(q: pt); w: pt); begin s(w) end;\n"
        "procedure runvec(procedure s(q: vec); w: vec); begin s(w) end;\n"
        "procedure runs(procedure s(t: string)); begin s('hello') end;\n"
        "begin\n"
        "  a.x := 3; a.y := 4; runpt(showpt, a);\n"
        "  v[1] := 1; v[2] := 2; v[3] := 3; runvec(showvec, v);\n"
        "  runs(shows)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n6\nhello\n");
}

TEST(ProcParams, ForwardDeclaredProcedureCanBePassed) {
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "function later(x: integer): integer; forward;\n"
        "function later(x: integer): integer; begin later := x + 9 end;\n"
        "begin writeln(ap(later, 1)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

TEST(ProcParams, ParameterShadowsAProcedureOfTheSameName) {
    auto R = compileAndRun(
        "program p;\n"
        "function d(x: integer): integer; begin d := x * 100 end;\n"
        "function ap(function d(x: integer): integer): integer;\n"
        "begin ap := d(2) end;\n"
        "function e(x: integer): integer; begin e := x + 1 end;\n"
        "begin writeln(ap(e)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

// --- EP parameter forms that carry hidden arguments of their own ---
//
// A conformant array passes bounds beside the array and a schema passes
// discriminants beside the body.  Both have to be expanded into the signature
// a procedural parameter is called through, or the call will not match the
// procedure that receives it.

TEST(ProcParams, ConformantArrayInsideAProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure show(a: array[lo..hi: integer] of integer);\n"
        "var i: integer;\n"
        "begin for i := lo to hi do write(a[i]:1); writeln end;\n"
        "procedure run(procedure s(a: array[u..v: integer] of integer));\n"
        "var small: array[1..3] of integer;\n"
        "    big:   array[1..5] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do small[i] := i;\n"
        "  for i := 1 to 5 do big[i] := i;\n"
        "  s(small); s(big)\n"
        "end;\n"
        "begin run(show) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "123\n12345\n");
}

TEST(ProcParams, SchemaInsideAProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "procedure show(v: vec);\n"
        "var i: integer;\n"
        "begin for i := 1 to 3 do write(v[i]:1); writeln end;\n"
        "procedure run(procedure s(v: vec));\n"
        "var a: vec(3);\n"
        "begin a[1] := 4; a[2] := 5; a[3] := 6; s(a) end;\n"
        "begin run(show) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "456\n");
}

// --- ISO §6.6.3.6: congruity ---

TEST(ProcParams, ParameterTypesMustBeIdentical) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer;\n"
        "begin ap := f(1) end;\n"
        "function bad(x: real): integer; begin bad := 1 end;\n"
        "begin writeln(ap(bad)) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
    // The message has to name both signatures, or it says nothing useful.
    EXPECT_NE(R.Stderr.find("function(real): integer"), std::string::npos)
        << R.Stderr;
    EXPECT_NE(R.Stderr.find("function(integer): integer"), std::string::npos)
        << R.Stderr;
}

TEST(ProcParams, ResultTypeMustMatch) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer;\n"
        "begin ap := f(1) end;\n"
        "function bad(x: integer): real; begin bad := 1.0 end;\n"
        "begin writeln(ap(bad)) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
}

TEST(ProcParams, ParameterCountMustMatch) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer;\n"
        "begin ap := f(1) end;\n"
        "function bad(x, y: integer): integer; begin bad := 1 end;\n"
        "begin writeln(ap(bad)) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
}

TEST(ProcParams, VarnessMustMatch) {
    // One passes an address and the other a copy, so this is not a detail.
    auto R = compileAndEmitIR(
        "program p;\n"
        "var n: integer;\n"
        "procedure run(procedure a(var x: integer));\n"
        "begin a(n) end;\n"
        "procedure act(x: integer); begin end;\n"
        "begin run(act) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("procedure(var integer)"), std::string::npos)
        << R.Stderr;
}

TEST(ProcParams, AProcedureCannotFillAFunctionalParameter) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer;\n"
        "begin ap := f(1) end;\n"
        "procedure noret(x: integer); begin end;\n"
        "begin writeln(ap(noret)) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
}

TEST(ProcParams, RequiredProceduresCannotBePassed) {
    // ISO §6.6.3.1: writeln is variadic and has no heading to be congruous
    // with, so there is nothing that could be checked.
    auto R = compileAndEmitIR(
        "program p;\n"
        "procedure run(procedure a(x: integer));\n"
        "begin a(1) end;\n"
        "begin run(writeln) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("required procedure"), std::string::npos)
        << R.Stderr;
}

TEST(ProcParams, TheArgumentMustBeAProcedureName) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "procedure run(procedure a(x: integer));\n"
        "begin a(1) end;\n"
        "begin run(42) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("must be the name of a procedure"),
              std::string::npos) << R.Stderr;
}

TEST(ProcParams, TheParameterCannotBeAssignedTo) {
    // It reads like the enclosing function's result variable, so the
    // diagnostic names it rather than only refusing the assignment.
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer;\n"
        "begin f := 3; ap := 1 end;\n"
        "function d(x: integer): integer; begin d := x end;\n"
        "begin writeln(ap(d)) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("'f' is a procedural parameter"),
              std::string::npos) << R.Stderr;
}

TEST(ProcParams, ConformantElementTypeMustStillMatch) {
    // Congruity for these is structural because they are not interned; that
    // must not slacken into accepting anything shaped like an array.
    auto R = compileAndEmitIR(
        "program p;\n"
        "procedure show(a: array[lo..hi: integer] of real);\n"
        "begin writeln(a[lo]:0:1) end;\n"
        "procedure run(procedure s(a: array[u..v: integer] of integer));\n"
        "var arr: array[1..2] of integer;\n"
        "begin arr[1] := 1; s(arr) end;\n"
        "begin run(show) end.\n", kEP);
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not congruous"), std::string::npos) << R.Stderr;
}

TEST(ProcParams, TheCallGoesThroughAPointerAndCarriesAFrame) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function addbase(x: integer): integer;\n"
        "  begin addbase := x + base end;\n"
        "begin base := 1; writeln(ap(addbase, 2)) end;\n"
        "begin outer end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    // ap takes the pair, and addbase is wrapped so that a target needing no
    // frame would still fit the same signature.
    EXPECT_TRUE(irContainsAll(R.IR, {"define i64 @pas_ap(ptr", "asparam"}))
        << R.IR;
}

// A procedural parameter arrives in registers, but a nested procedure reaches
// an outer variable through a static link, which carries addresses and nothing
// else.  The pair is therefore spilled to a cell so it has one — without that
// the frame slot took a null and the compiler crashed building it.

TEST(ProcParams, ANestedProcedureCanCallAnOuterProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "  function helper(y: integer): integer;\n"
        "  begin helper := f(y) * 10 end;\n"
        "begin ap := helper(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "begin writeln(ap(dbl, 3)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "60\n");
}

TEST(ProcParams, ANestedProcedureCanPassAnOuterProceduralParameterOn) {
    auto R = compileAndRun(
        "program p;\n"
        "function call2(function g(x: integer): integer; v: integer): integer;\n"
        "begin call2 := g(v) end;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "  function helper(y: integer): integer;\n"
        "  begin helper := call2(f, y) + 1 end;\n"
        "begin ap := helper(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "begin writeln(ap(dbl, 5)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n");
}

TEST(ProcParams, TwoLevelsDownStillReachesTheParameter) {
    // Both frames are in play: l2 reaches f through l1's link, and f is itself
    // a nested procedure holding a frame of its own.
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "  function l1(y: integer): integer;\n"
        "    function l2(z: integer): integer;\n"
        "    begin l2 := f(z) * 100 end;\n"
        "  begin l1 := l2(y) end;\n"
        "begin ap := l1(v) end;\n"
        "procedure outer;\n"
        "var base: integer;\n"
        "  function addbase(x: integer): integer;\n"
        "  begin addbase := x + base end;\n"
        "begin base := 3; writeln(ap(addbase, 4)) end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "700\n");
}

// A forward heading and its implementation resolve the same parameter twice.
// For the parameter forms that are not interned, comparing the two results by
// identity reported that a type did not match itself.

TEST(ProcParams, ForwardDeclarationMayTakeAProceduralParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "  forward;\n"
        "function ap(function f(x: integer): integer; v: integer): integer;\n"
        "begin ap := f(v) end;\n"
        "function dbl(x: integer): integer; begin dbl := x * 2 end;\n"
        "begin writeln(ap(dbl, 21)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ProcParams, ForwardDeclarationMayTakeAConformantArray) {
    // Broken before procedural parameters existed, by the same cause.
    auto R = compileAndRun(
        "program p;\n"
        "var arr: array[1..2] of integer;\n"
        "procedure show(a: array[lo..hi: integer] of integer); forward;\n"
        "procedure show(a: array[lo..hi: integer] of integer);\n"
        "begin writeln(a[lo]) end;\n"
        "begin arr[1] := 5; show(arr) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(ProcParams, AForwardSignatureThatDiffersIsStillRejected) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "function ap(function f(x: integer): integer): integer; forward;\n"
        "function ap(function f(x: real): integer): integer;\n"
        "begin ap := 1 end;\n"
        "begin writeln(1) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("does not match forward declaration"),
              std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.11.2: module finalisers unwind in reverse initialization order
// ---------------------------------------------------------------------------

TEST(ModuleLifecycle, FinalisersRunInReverseOrder) {
    auto R = compileAndRun(
        "module A;\n"
        " to begin do writeln('A init');\n"
        " to end do writeln('A fini');\n"
        "end.\n"
        "module B; import A;\n"
        " to begin do writeln('B init');\n"
        " to end do writeln('B fini');\n"
        "end.\n"
        "program p(output); import B; begin writeln('body') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A init\nB init\nbody\nB fini\nA fini\n");
}

TEST(ModuleLifecycle, AModuleNotImportedIsStillInitialized) {
    // It is in this compilation unit, so its variables exist and its 'to begin
    // do' has to have run before anything could reach them.
    auto R = compileAndRun(
        "module Solo;\n"
        " to begin do writeln('solo init');\n"
        " to end do writeln('solo fini');\n"
        "end.\n"
        "program p(output); begin writeln('body') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "solo init\nbody\nsolo fini\n");
}

// A separately compiled module's lifecycle blocks never ran at all: the
// initialiser had internal linkage and was called only from a main() emitted
// in the same unit, and a program importing it emits its own main.
TEST(ModuleLifecycle, RunsWhenTheModuleIsCompiledSeparately) {
    auto R = compileTwoFiles(
        "module LifeAlone;\n"
        "  var v: integer;\n"
        "  to begin do begin v := 7; writeln('init') end;\n"
        "  to end do writeln('fini');\n"
        "end.\n",
        "program p(output); import LifeAlone;\n"
        "begin writeln('body v=', v) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "init\nbody v=7\nfini\n");
}

TEST(ModuleLifecycle, ATransitivelyImportedModuleComesUpFirst) {
    // The program imports only LifeMid, and cannot see that LifeMid imports
    // LifeBase — that is in the other object.  LifeBase must come up first all
    // the same, since LifeMid's initialiser reads what it set.
    auto R = compileTwoFiles(
        "module LifeBase;\n"
        "  var v: integer;\n"
        "  to begin do begin v := 1; writeln('init base') end;\n"
        "  to end do writeln('fini base');\n"
        "end.\n"
        "module LifeMid;\n"
        "  import LifeBase;\n"
        "  var w: integer;\n"
        "  to begin do begin w := v + 1; writeln('init mid') end;\n"
        "  to end do writeln('fini mid');\n"
        "end.\n",
        "program p(output); import LifeMid;\n"
        "begin writeln('body w=', w) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "init base\ninit mid\nbody w=2\nfini mid\nfini base\n");
}

TEST(ModuleLifecycle, ADiamondInitialisesTheSharedModuleOnce) {
    auto R = compileTwoFiles(
        "module DiaBase;\n"
        "  to begin do writeln('init base');\n"
        "  to end do writeln('fini base');\n"
        "end.\n"
        "module DiaLeft; import DiaBase;\n"
        "  to begin do writeln('init left');\n"
        "  to end do writeln('fini left');\n"
        "end.\n"
        "module DiaRight; import DiaBase;\n"
        "  to begin do writeln('init right');\n"
        "  to end do writeln('fini right');\n"
        "end.\n",
        "program p(output); import DiaLeft; import DiaRight;\n"
        "begin writeln('body') end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "init base\ninit left\ninit right\nbody\n"
              "fini right\nfini left\nfini base\n");
}

// ---------------------------------------------------------------------------
// EP §6.11.3: one identifier may not name two imported objects
// ---------------------------------------------------------------------------

TEST(ImportClash, TwoModulesExportingOneNameIsAnError) {
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end; end.\n"
        "module B; function f: integer; begin f := 2 end; end.\n"
        "program p(output); import A; import B; begin writeln(f) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("imported from both"), std::string::npos) << R.Stderr;
}

TEST(ImportClash, DistinctNamesAreFine) {
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end; end.\n"
        "module B; function g: integer; begin g := 2 end; end.\n"
        "program p(output); import A; import B;\n"
        "begin writeln(f, ' ', g) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

TEST(ImportClash, AnOnlyClauseAvoidsTheClash) {
    // A's f is not imported, so f is B's and prints 2.  A's f is still emitted,
    // and used to win the name: both were mangled plang_f, and A came first.
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end;\n"
        " function h: integer; begin h := 9 end; end.\n"
        "module B; function f: integer; begin f := 2 end; end.\n"
        "program p(output); import A only h; import B;\n"
        "begin writeln(h, ' ', f) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 2\n");
}

// ---------------------------------------------------------------------------
// EP §6.11: a module-level name is mangled with its module
//
// Two modules may each declare an `f`.  They used to be emitted as one symbol,
// plang_f: LLVM renamed the second definition to plang_f.1, every caller was
// sent to the first, and the second was never reached.  Nothing was reported.
// ---------------------------------------------------------------------------

TEST(ModuleMangling, QualifiedCallsReachTheirOwnModule) {
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end; end.\n"
        "module B; function f: integer; begin f := 2 end; end.\n"
        "program p(output); import A qualified; import B qualified;\n"
        "begin writeln(A.f, ' ', B.f) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

TEST(ModuleMangling, TwoModulesMayDeclareTheSameVariable) {
    auto R = compileAndRun(
        "module A; var v: integer; procedure set_; begin v := 1 end; end.\n"
        "module B; var v: integer; procedure set_; begin v := 2 end; end.\n"
        "program p(output); import A qualified; import B qualified;\n"
        "begin A.set_; B.set_; writeln(A.v, ' ', B.v) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

TEST(ModuleMangling, APrivateHelperIsNotSharedBetweenModules) {
    // Neither `helper` is imported anywhere, so each module must call its own.
    auto R = compileAndRun(
        "module A; var v: integer;\n"
        "  procedure helper; begin v := v + 10 end;\n"
        "  procedure bump; begin helper end;\n"
        "  function get: integer; begin get := v end; end.\n"
        "module B; var v: integer;\n"
        "  procedure helper; begin v := v + 100 end;\n"
        "  procedure bump; begin helper end;\n"
        "  function get: integer; begin get := v end; end.\n"
        "program p(output); import A qualified; import B qualified;\n"
        "begin A.bump; B.bump; B.bump; writeln(A.get, ' ', B.get) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10 200\n");
}

TEST(ModuleMangling, ToBeginDoWritesItsOwnModulesVariable) {
    auto R = compileAndRun(
        "module A; var v: integer; to begin do v := 1; end.\n"
        "module B; var v: integer; to begin do v := 2; end.\n"
        "program p(output); import A qualified; import B qualified;\n"
        "begin writeln(A.v, ' ', B.v) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

TEST(ModuleMangling, AModulesOwnDeclarationBeatsOneItImports) {
    // B declares f and imports A's f.  Its own must win, both inside B and for
    // the program calling B.f.
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end;\n"
        "  function g: integer; begin g := 100 end; end.\n"
        "module B; import A;\n"
        "  function f: integer; begin f := 2 + g end;\n"
        "  procedure show; begin writeln(f) end; end.\n"
        "program p(output); import B qualified;\n"
        "begin B.show; writeln(B.f) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "102\n102\n");
}

// An enclosing scope is joined to what it declares with `$`, which no Pascal
// identifier can contain.  Joined with `__`, as it was before 0.1.3, a mangled
// name did not separate into its parts one way: EP §6.1.3 allows an underscore
// inside an identifier, so module `a`'s `b` and a top-level `a__b` were both
// `pas_a__b`.  Nothing reported it — the second definition was renamed by LLVM
// and every call went to the first, so this printed 1 twice.
TEST(ModuleMangling, AnUnderscoreInANameIsNotAScopeSeparator) {
    auto R = compileAndRun(
        "module a; function b: integer; begin b := 1 end; end.\n"
        "program p(output); import a;\n"
        "  function a__b: integer; begin a__b := 2 end;\n"
        "begin writeln(b); writeln(a__b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n2\n");
}

TEST(ModuleMangling, TheScopeSeparatorReachesTheObjectFile) {
    auto R = compileAndEmitIR(
        "module a; function b: integer; begin b := 1 end; end.\n"
        "program p(output); import a;\n"
        "begin writeln(b) end.\n", kEP);
    EXPECT_TRUE(irContainsAll(R.IR, {"@\"pas_a$b\""})) << R.IR;
}

// .pmi files are written next to the source, and these tests share /tmp, so a
// separately compiled module needs a name no other test will write over.
TEST(ModuleMangling, SurvivesSeparateCompilation) {
    auto R = compileTwoFiles(
        "module MangleLeft;\n"
        "  var v: integer;\n"
        "  function f: integer; begin f := 1 end;\n"
        "  procedure bump; begin v := v + 10 end;\n"
        "end.\n"
        "module MangleRight;\n"
        "  var v: integer;\n"
        "  function f: integer; begin f := 2 end;\n"
        "  procedure bump; begin v := v + 100 end;\n"
        "end.\n",
        "program p(output);\n"
        "import MangleLeft qualified; import MangleRight qualified;\n"
        "begin\n"
        "  MangleLeft.bump; MangleRight.bump;\n"
        "  writeln(MangleLeft.f, ' ', MangleRight.f, ' ',\n"
        "          MangleLeft.v, ' ', MangleRight.v)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 10 100\n");
}

TEST(ModuleMangling, AnImportedParameterlessFunctionIsCalledNotRead) {
    // Separately compiled, so nothing named `answer` has been emitted here to
    // recognize it by.  Read as a variable it links against a global that does
    // not exist.
    auto R = compileTwoFiles(
        "module MangleNullary;\n"
        "function answer: integer; begin answer := 42 end; end.\n",
        "program p(output); import MangleNullary;\n"
        "begin writeln(answer) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/link/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ImportClash, ReachingOneModuleTwiceIsNotAClash) {
    // A is imported directly and again through B; both paths reach the same
    // declaration, which the rule is not about.
    auto R = compileAndRun(
        "module A; function f: integer; begin f := 1 end; end.\n"
        "module B; import A; function g: integer; begin g := f + 1 end; end.\n"
        "program p(output); import A; import B;\n"
        "begin writeln(f, ' ', g) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2\n");
}

// ---------------------------------------------------------------------------
// EP §6.11.1 module-identification and the block that follows a heading
// ---------------------------------------------------------------------------

TEST(EP11ModuleImplementation, IsWrittenAsADirectiveAfterTheName) {
    auto R = compileAndRun(
        "module Arith interface;\n"
        "export Arith = (Twice);\n"
        "function Twice(x: integer): integer;\n"
        "end.\n"
        "module Arith implementation;\n"
        "function Twice(x: integer): integer;\n"
        "begin Twice := x + x end;\n"
        "end.\n"
        "program p(output);\n"
        "import Arith;\n"
        "begin writeln(Twice(21)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// EP §6.11.1: the interface's declarations are the implementation's too, so
// a type it keeps to itself is still there to be used.
TEST(EP11ModuleImplementation, IsGivenWhatTheInterfaceDeclares) {
    auto R = compileAndRun(
        "module Arith interface;\n"
        "export Arith = (Twice);\n"
        "type Row = array[1..3] of integer;\n"
        "function Twice(x: integer): integer;\n"
        "end.\n"
        "module Arith implementation;\n"
        "function Twice(x: integer): integer;\n"
        "var r: Row;\n"
        "begin r[1] := x + x; Twice := r[1] end;\n"
        "end.\n"
        "program p(output);\n"
        "import Arith;\n"
        "begin writeln(Twice(21)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// EP §6.11.1: module-declaration = module-heading [ ';' module-block ], so
// the block may follow its heading in the one declaration, and a body there
// is written as the name alone.
TEST(EP11ModuleImplementation, FollowsItsHeadingInTheSameDeclaration) {
    auto R = compileAndRun(
        "module Arith interface;\n"
        "export Arith = (Twice, Counter);\n"
        "var Counter: integer;\n"
        "function Twice(x: integer): integer;\n"
        "end;\n"
        "function Twice;\n"
        "begin Counter := Counter + 1; Twice := x + x end;\n"
        "to begin do Counter := 0;\n"
        "end.\n"
        "program p(output);\n"
        "import Arith;\n"
        "begin writeln(Twice(21)); writeln(Counter) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n1\n");
}

// ---------------------------------------------------------------------------
// EP §6.6 Initial states
// ---------------------------------------------------------------------------

// The 'value' clause belongs to the type-denoter, so a type can say what
// state every variable of it begins in, and not only a variable can.
TEST(EP6InitialState, ComesFromTheTypeAVariableIsDeclaredWith) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type counter = integer value 7;\n"
        "     tally   = counter;\n"
        "var n: counter; m: tally;\n"
        "begin writeln(n, ' ', m) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 7\n");
}

TEST(EP6InitialState, IsWrittenForARecordAsARecordValue) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type point = record x, y: integer end value [x: 3; y: 4];\n"
        "var q: point;\n"
        "begin writeln(q.x, ' ', q.y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 4\n");
}

// A record-section may carry one of its own, which is how a record is given
// an initial state a field at a time.
TEST(EP6InitialState, IsWrittenFieldByFieldToo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type flags = record a: integer value 1; b: real value 2.5;\n"
        "                    c: integer end;\n"
        "var f: flags;\n"
        "begin f.c := 3; writeln(f.a, ' ', f.b:0:1, ' ', f.c) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2.5 3\n");
}

TEST(EP6InitialState, IsWrittenForAnArrayAsAnArrayValue) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type stars = array [1..5] of char value [1..4: '*'; otherwise '.'];\n"
        "var s: stars; i: integer;\n"
        "begin for i := 1 to 5 do write(s[i]); writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "****.\n");
}

// An array of a type that begins somewhere begins there in every element,
// however many elements it has.
TEST(EP6InitialState, ReachesEveryElementOfALongArray) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = integer value 2;\n"
        "     big = array [1..100] of k;\n"
        "var h: big; i, sum: integer;\n"
        "begin sum := 0; for i := 1 to 100 do sum := sum + h[i];\n"
        "  writeln(h[1], ' ', h[100], ' ', sum) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 2 200\n");
}

// EP §6.8.7.1: a component-value names no type, the one it is a value of
// being the one the place it is written calls for — so these values nest.
TEST(EP6InitialState, TakesValuesWithinValues) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type point = record x, y: integer end;\n"
        "     line  = record a, b: point end\n"
        "               value [a: [x: 1; y: 2]; b: [x: 3; y: 4]];\n"
        "     grid  = array [1..3] of point value [otherwise [x: 9; y: 9]];\n"
        "var l: line; g: grid;\n"
        "begin writeln(l.a.x, l.a.y, l.b.x, l.b.y);\n"
        "  writeln(g[1].x, g[3].y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1234\n99\n");
}

TEST(EP6InitialState, HoldsForStringsAndSetsAndCharacterStrings) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type name   = string(10) value 'hi';\n"
        "     digits = set of 0..9 value [1, 3..4];\n"
        "     tag    = packed array [1..3] of char value 'abc';\n"
        "var s: name; d: digits; t: tag;\n"
        "begin writeln(s, length(s));\n"
        "  writeln(1 in d, ' ', 2 in d, ' ', 4 in d);\n"
        "  writeln(t) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi2\ntrue false true\nabc\n");
}

// A local variable is a variable, so it begins in that state on every call.
TEST(EP6InitialState, IsRestoredOnEveryEntryToABlock) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type counter = integer value 5;\n"
        "procedure bump;\n"
        "var n: counter;\n"
        "begin n := n + 1; write(n, ' ') end;\n"
        "begin bump; bump; writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6 6 \n");
}

// EP §6.7.5.3: the variable new creates is a variable of the domain type and
// begins where one of that type begins.
TEST(EP6InitialState, GreetsTheVariableThatNewCreates) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = integer value 5;\n"
        "     r = record a: k; b: integer value 9 end;\n"
        "     pr = ^r;\n"
        "var q: pr;\n"
        "begin new(q); writeln(q^.a, ' ', q^.b); dispose(q) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 9\n");
}

// The value has to be one of the type the denoter denotes.
TEST(EP6InitialState, TurnsAwayAValueOfAnotherType) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type bad = integer value 'x';\n"
        "var b: bad;\n"
        "begin writeln(b) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not compatible"), std::string::npos) << R.Stderr;
}

// EP §6.6 note 3: the specifier goes with the whole denoter and not with the
// component type it stands beside, so a character is not eight characters.
TEST(EP6InitialState, BelongsToTheWholeDenoterAndNotToItsComponent) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type bad = array [1..8] of char value '*';\n"
        "var c: bad;\n"
        "begin writeln(c[1]) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not compatible"), std::string::npos) << R.Stderr;
}

TEST(EP6InitialState, IsNotStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type counter = integer value 7;\n"
        "var n: counter;\n"
        "begin writeln(n) end.\n");
    EXPECT_NE(R.ExitCode, 0);
}

// A type an interface declares travels to the units that import it, and the
// state it says its variables begin in travels with it.
TEST(EP6InitialState, TravelsWithAnExportedType) {
    auto R = compileTwoFiles(
        "module Figures interface;\n"
        "export Figures = (point, counter);\n"
        "type point = record x, y: integer end value [x: 3; y: 4];\n"
        "     counter = integer value 7;\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Figures;\n"
        "var q: point; n: counter;\n"
        "begin writeln(q.x, q.y, ' ', n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "34 7\n");
}

// EP §6.11.1: the interface is what a .pmi has to carry, and a body declares
// none of the types and writes its routines as the name alone.
TEST(SeparateCompilation, CarriesWhatTheInterfaceDeclaresAndNotWhatTheBodyRepeats) {
    auto R = compileTwoFiles(
        "module Plain interface;\n"
        "export Plain = (pt, twice);\n"
        "type pt = record x, y: integer end;\n"
        "function twice(v: integer): integer;\n"
        "end;\n"
        "function twice;\n"
        "begin twice := v + v end;\n"
        "end.\n",
        "program p(output);\n"
        "import Plain;\n"
        "var q: pt;\n"
        "begin q.x := 21; writeln(twice(q.x)) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// ---------------------------------------------------------------------------
// EP §6.11: constants across a .pmi
// ---------------------------------------------------------------------------

// A constant an interface declares is one an importing unit may use, and one
// the interface's own types are often written in terms of.
TEST(SeparateCompilation, ConstantsCrossTheInterfaceFile) {
    auto R = compileTwoFiles(
        "module Limits interface;\n"
        "export Limits = (MaxRows, Greeting, Half, Row);\n"
        "const MaxRows = 5;\n"
        "      Greeting = 'hi';\n"
        "      Half = MaxRows div 2;\n"
        "type Row = array [1..MaxRows] of integer;\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Limits;\n"
        "var r: Row;\n"
        "begin r[MaxRows] := 9;\n"
        "  writeln(MaxRows, ' ', Greeting, ' ', Half, ' ', r[5]) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 hi 2 9\n");
}

TEST(SeparateCompilation, CarriesConstantsOfEveryScalarKind) {
    auto R = compileTwoFiles(
        "module Kinds interface;\n"
        "export Kinds = (Ratio, Neg, Star, Yes, Big);\n"
        "const Ratio = 3.5;\n"
        "      Neg = -7;\n"
        "      Star = '*';\n"
        "      Yes = true;\n"
        "      Big = 'hello world';\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Kinds;\n"
        "begin writeln(Ratio:0:1, ' ', Neg, ' ', Star, ' ', Yes, ' ', Big) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.5 -7 * true hello world\n");
}

// EP §6.8.7: a structured constant names the type it is a value of, so it is
// written after the types rather than before them.
TEST(SeparateCompilation, CarriesAStructuredConstantAfterItsType) {
    auto R = compileTwoFiles(
        "module Places interface;\n"
        "export Places = (Spot, Origin);\n"
        "type Spot = record x, y: integer end;\n"
        "const Origin = Spot[x: 1; y: 2];\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Places;\n"
        "var q: Spot;\n"
        "begin q := Origin; writeln(q.x, q.y) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "12\n");
}

// ISO §6.2.2.10: the required constants are declared in a region enclosing the
// program, so a program that declares one of the names again means its own.
// A variable named `pi` was written to but never read back: the read found
// 3.14159 while the write had gone to the variable.
TEST(RequiredIdentifiers, AVariableMayTakeTheNameOfARequiredConstant) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var pi: real; maxint: integer;\n"
        "begin pi := 3.5; maxint := 7; writeln(pi:0:1, ' ', maxint) end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.5 7\n");
}

TEST(RequiredIdentifiers, AConstantMayTakeTheNameOfARequiredConstant) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const pi = 3.5;\n"
        "begin writeln(pi:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.5\n");
}

// The same when the declaration is an imported one: what the module says
// stands, and the value the language gives stands only where nothing else does.
TEST(RequiredIdentifiers, AnImportedConstantTakesTheNameToo) {
    auto R = compileAndRun(
        "module Circle interface;\n"
        "export Circle = (pi);\n"
        "const pi = 3.5;\n"
        "end;\n"
        "end.\n"
        "program p(output);\n"
        "import Circle;\n"
        "begin writeln(pi:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.5\n");
}

TEST(RequiredIdentifiers, StandWhereNothingElseDoes) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(maxint, ' ', pi:0:5) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9223372036854775807 3.14159\n");
}

// EP §6.4.7: a schema crosses the interface file with its discriminants, and
// a routine that takes one is called with them.  The writer used to put
// `integer` where it could not write the type down, so the importer passed the
// array by value to a module reading a pointer and a bound.
TEST(SeparateCompilation, ASchemaTypeCrossesTheInterfaceFile) {
    auto R = compileTwoFiles(
        "module Sch interface;\n"
        "export Sch = (vector, Sum);\n"
        "type vector(n: integer) = array[1..n] of integer;\n"
        "function Sum(var v: vector): integer;\n"
        "end;\n"
        "function Sum(var v: vector): integer;\n"
        "var i, s: integer;\n"
        "begin s := 0; for i := 1 to v.n do s := s + v[i]; Sum := s end;\n"
        "end.\n",
        "program p(output);\n"
        "import Sch;\n"
        "var v: vector(4); i: integer;\n"
        "begin for i := 1 to 4 do v[i] := i; writeln(Sum(v)) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

// Two units have to agree on where a discriminated instance keeps things, and
// the extents of a record body are worked out separately in each of them.
TEST(SeparateCompilation, ARecordSchemaIsLaidOutTheSameInBothUnits) {
    auto R = compileTwoFiles(
        "module Shp interface;\n"
        "export Shp = (poly, mk);\n"
        "type poly(n: integer) = record deg: integer; c: array[0..n] of real end;\n"
        "procedure mk(var q: poly(2));\n"
        "end;\n"
        "procedure mk(var q: poly(2));\n"
        "var i: integer;\n"
        "begin q.deg := 2; for i := 0 to 2 do q.c[i] := i + 0.25 end;\n"
        "end.\n",
        "program p(output);\n"
        "import Shp;\n"
        "var a: poly(2); b: poly(4); i: integer;\n"
        "begin\n"
        "  mk(a);\n"
        "  for i := 0 to 4 do b.c[i] := 900 + i;\n"
        "  writeln(a.deg:0, ' ', a.c[2]:0:2, ' ', b.c[4]:0:1)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 2.25 904.0\n");
}

// ISO §6.6.3.7 and §6.6.3.1: the bounds of a conformant array parameter and
// the heading of a procedural one are hidden arguments, and the importer can
// only pass them if the interface file says they are there.
TEST(SeparateCompilation, ConformantAndProceduralParametersCross) {
    auto R = compileTwoFiles(
        "module Ap interface;\n"
        "export Ap = (Show, Apply, Neg);\n"
        "procedure Show(a: array[lo..hi: integer] of integer);\n"
        "function Apply(function f(x: integer): integer; v: integer): integer;\n"
        "function Neg(x: integer): integer;\n"
        "end;\n"
        "procedure Show(a: array[lo..hi: integer] of integer);\n"
        "var i: integer;\n"
        "begin for i := lo to hi do write(a[i], ' '); writeln end;\n"
        "function Apply(function f(x: integer): integer; v: integer): integer;\n"
        "begin Apply := f(v) end;\n"
        "function Neg(x: integer): integer;\n"
        "begin Neg := -x end;\n"
        "end.\n",
        "program p(output);\n"
        "import Ap;\n"
        "var a: array[1..3] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do a[i] := i * 7;\n"
        "  Show(a);\n"
        "  writeln(Apply(Neg, 5))\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 14 21 \n-5\n");
}

// ISO §6.4.3.3: a record's variants are as much of it as the fixed part, and
// were left out of the interface file entirely — an importer laid out a record
// short of them and wrote past its end.
TEST(SeparateCompilation, AVariantRecordCrossesWholeAndPacked) {
    auto R = compileTwoFiles(
        "module Vr interface;\n"
        "export Vr = (shape, kind, circ..rect, mk, area);\n"
        "type kind = (circ, rect);\n"
        "     shape = record\n"
        "       name: packed array[1..4] of char;\n"
        "       case k: kind of\n"
        "         circ: (r: real);\n"
        "         rect: (w, h: real)\n"
        "     end;\n"
        "function mk(kk: kind; a, b: real): shape;\n"
        "function area(s: shape): real;\n"
        "end;\n"
        "function mk(kk: kind; a, b: real): shape;\n"
        "var s: shape;\n"
        "begin\n"
        "  s.name := 'abcd'; s.k := kk;\n"
        "  if kk = circ then s.r := a else begin s.w := a; s.h := b end;\n"
        "  mk := s\n"
        "end;\n"
        "function area(s: shape): real;\n"
        "begin\n"
        "  if s.k = circ then area := 3.0 * s.r * s.r\n"
        "  else area := s.w * s.h\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Vr;\n"
        "var s: shape;\n"
        "begin\n"
        "  s := mk(rect, 2.0, 3.0);\n"
        "  writeln(s.name, ' ', area(s):0:1);\n"
        "  s := mk(circ, 2.0, 0.0);\n"
        "  writeln(area(s):0:1)\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcd 6.0\n12.0\n");
}

// An imported variable is reached through its declaration, and the index range
// of an array is written there and nowhere in the storage: `array[1..2]` was
// read as 0..1 and every access to it was out of bounds.
TEST(SeparateCompilation, AnImportedArrayKeepsItsIndexRange) {
    auto R = compileTwoFiles(
        "module Bd interface;\n"
        "export Bd = (grid, Board);\n"
        "type grid = array[1..2, 1..2] of integer;\n"
        "var Board: grid;\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Bd;\n"
        "var i, j: integer;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 2 do Board[i, j] := i * j;\n"
        "  writeln(Board[2, 2])\n"
        "end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4\n");
}

// EP §6.11.2: an export-range names a run of constants from its first to its
// last.  The first was written as the range's own name and dropped for it.
TEST(ExportList, TheFirstConstantOfARangeIsExported) {
    auto R = compileAndRun(
        "module pal interface;\n"
        "  export pal = (color, red..green);\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "module pal;\n"
        "  type color = (red, orange, yellow, green, blue);\n"
        "end.\n"
        "program p(output);\n"
        "  import pal;\n"
        "var c: color;\n"
        "begin c := red; writeln(ord(c)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n");
}

// EP §6.8.2: a constant whose value only a running program can work out still
// belongs to the interface.  The module leaves the answer in storage of its
// own, and the file says what was written so the importer can name it.
TEST(SeparateCompilation, CarriesAConstantThatHasToBeComputed) {
    auto R = compileTwoFiles(
        "module Odd interface;\n"
        "export Odd = (N, M, S);\n"
        "const N = ord('a');\n"
        "      M = N + 3;\n"
        "      S = 'x' + 'y';\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import Odd;\n"
        "begin writeln(M, ' ', N, ' ', S) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "100 97 xy\n");
}

// The same constant in one file, where the importer sees the module itself
// rather than a written interface.
TEST(EP11Modules, ComputesAConstantOfItsOwnBeforeAnyoneReadsIt) {
    auto R = compileAndRun(
        "module Odd interface;\n"
        "export Odd = (N, M);\n"
        "const N = ord('a');\n"
        "      M = N + 3;\n"
        "end;\n"
        "end.\n"
        "program p(output);\n"
        "import Odd;\n"
        "begin writeln(M, ' ', N) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "100 97\n");
}

// EP §6.11.2: a module's initialiser is what brings its own variables up, and
// a `value` clause on one of them is as much a part of it as the program's.
TEST(EP6InitialState, ReachesAModulesOwnVariables) {
    auto R = compileAndRun(
        "module V interface;\n"
        "export V = (Counter, Tally);\n"
        "var Counter: integer value 5;\n"
        "type k = integer value 8;\n"
        "var Tally: k;\n"
        "end;\n"
        "end.\n"
        "program p(output);\n"
        "import V;\n"
        "begin writeln(Counter, ' ', Tally) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 8\n");
}

// The same for a structured constant a module declares: it is storage that
// something has to fill, and the module that declares it is that something.
TEST(EP11Modules, FillsInAStructuredConstantOfItsOwn) {
    auto R = compileAndRun(
        "module W interface;\n"
        "export W = (Spot, Corner);\n"
        "type Spot = record x, y: integer end;\n"
        "const Corner = Spot[x: 3; y: 4];\n"
        "end;\n"
        "end.\n"
        "program p(output);\n"
        "import W;\n"
        "var q: Spot;\n"
        "begin q := Corner; writeln(q.x, q.y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "34\n");
}

// A typed file is written in a representation of its own, and which one it is
// was decided by looking at the denoter the variable was declared with.  Under
// a type name that denoter is a name, and a name is not a `file of`, so the
// file went down the text path: `file of integer` wrote its values out as
// digits and read them back as one number with the digits run together.
TEST(Regression, ATypedFileIsBinaryThroughATypeName) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type intfile = file of integer;\n"
        "var f: intfile; i: integer;\n"
        "begin\n"
        "  rewrite(f); write(f, 11); write(f, 22); write(f, 33);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin read(f, i); writeln(i) end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(Regression, ATypedFileOfRecordsIsBinaryThroughATypeName) {
    // The element size comes from the same place, and one byte per record
    // would leave every field after the first behind.
    auto R = compileAndRun(
        "program p(output);\n"
        "type pt = record x, y: integer end;\n"
        "     ptfile = file of pt;\n"
        "var f: ptfile; v: pt;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v.x := 3; v.y := 4; write(f, v);\n"
        "  v.x := 5; v.y := 6; write(f, v);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin read(f, v); writeln(v.x, ' ', v.y) end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 4\n5 6\n");
}

TEST(Regression, AFileOfCharThroughATypeNameStaysText) {
    // ISO 6.4.3.5: a file of char is a text file, and looking the element type
    // up rather than reading its name off the denoter has to keep saying so.
    auto R = compileAndRun(
        "program p(output);\n"
        "type chfile = file of char;\n"
        "var f: chfile; c: char;\n"
        "begin\n"
        "  rewrite(f); write(f, 'h'); write(f, 'i');\n"
        "  reset(f);\n"
        "  while not eof(f) do begin read(f, c); write(c) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    // Trailing space: the line marker closing the line the writes left open.
    EXPECT_EQ(R.Stdout, "hi \n");
}

// ---------------------------------------------------------------------------
// EP §6.11: a constant means the same thing on both sides of an interface file
//
// The .pmi writer re-derives a constant's text, and the text was not always
// enough to say what the value had been.  Both of these are width taken from
// one side of a boundary, the same shape as the codegen defects 0.1.4 fixed.
// ---------------------------------------------------------------------------

TEST(InterfaceConstants, ARealCrossesWithoutLosingDigits) {
    // std::to_string gives six decimals, which is not a double: the module
    // computed with 3.14159265358979 and its importer with 3.141593, and a
    // constant below 1e-6 arrived as exactly zero.
    auto R = compileTwoFiles(
        "module M interface;\n"
        "export M = (Pi, Tiny);\n"
        "const Pi = 3.14159265358979;\n"
        "      Tiny = 0.000000001;\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import M;\n"
        "begin writeln(Pi:20:14); writeln(Tiny:22:12) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "    3.14159265358979\n        0.000000001000\n");
}

TEST(InterfaceConstants, AnApostropheInAStringSurvivesTheCrossing) {
    // ISO §6.1.7 writes an apostrophe twice.  The value went out as it stood,
    // so 'it''s' was recorded as 'it's' -- which the importer reads as `it`
    // followed by nonsense, and where the quote count stays even, reads
    // silently as a different string.
    auto R = compileTwoFiles(
        "module M interface;\n"
        "export M = (Q);\n"
        "const Q = 'it''s';\n"
        "end;\n"
        "end.\n",
        "program p(output);\n"
        "import M;\n"
        "begin writeln(Q) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "it's\n");
}

TEST(Schema, NewDoesNotSilentlyDiscardExtraArguments) {
    // §6.6.5.3 gives `new`'s extra arguments two readings and only two: variant
    // case-constants, or EP §6.7.5.3 discriminants.  A domain that is neither
    // had them checked as expressions and then dropped on the floor, so this
    // allocated one integer's worth and lost the 8 without a word.
    auto R = compileAndRun(
        "program p; var q: ^integer;\n"
        "begin new(q, 8) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("only for a record with a variant part"),
              std::string::npos) << R.Stderr;
}

TEST(Schema, APointerToStringTakesItsCapacityFromNew) {
    // EP §6.4.3.3 makes `string` a schema whose one discriminant is its
    // capacity, so a bare `string` is a legal pointer domain-type and `new(q, 20)`
    // says how wide the string is.  plang used to read the bare name as the
    // unbounded string wherever it appeared: the 20 was dropped on the floor,
    // a pointer's worth was allocated, and `q^ := '...'` wrote a pointer into
    // it and read back an empty string of length 1.
    auto R = compileAndRun(
        "program p(output); type ps = ^string; var q: ps;\n"
        "begin new(q, 20); q^ := 'a string schema';\n"
        "      writeln('[', q^, '] len=', length(q^):1); dispose(q) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[a string schema] len=15\n");
}

TEST(Schema, APointerToStringChecksAgainstTheCapacityItWasGiven) {
    // The capacity is the one new() was given, not the probe binding the body
    // was resolved against -- which would check every such assignment against
    // a string(1).  Sema cannot decide this one, so it is a run-time check.
    auto R = compileAndRun(
        "program p(output); type ps = ^string; var q: ps;\n"
        "begin new(q, 4); q^ := 'far too long' end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("string(4)"), std::string::npos) << R.Stderr;
}

TEST(Schema, AVariantRecordStillTakesItsTagsInNew) {
    // The direction the check must not break: for a record with a variant part
    // the extra arguments are case-constants and the call is ordinary Pascal.
    auto R = compileAndRun(
        "program p(output);\n"
        "type shape = (circ, rect);\n"
        "     fig = record case k: shape of circ: (r: integer);\n"
        "                                   rect: (w, h: integer) end;\n"
        "var q: ^fig;\n"
        "begin new(q, circ); q^.r := 5; writeln('r=', q^.r:1); dispose(q, circ) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "r=5\n");
}

TEST(Schema, APointerToStringSurvivesASecondAssignment) {
    // The body sits AFTER the discriminant header new() wrote, and two places
    // answered "where is q^'s storage" -- emitLValue and schemaRefOf -- which
    // differed by the header size.  So the length field and the capacity
    // discriminant were the same eight bytes: `q^ := 'first'` stored 5 over the
    // capacity 20, and the next assignment was checked against 5.  A single
    // assignment hid it, because the capacity is loaded before the store that
    // destroys it, and reads were self-consistent at the wrong address.
    auto R = compileAndRun(
        "program p(output); type ps = ^string; var q: ps;\n"
        "begin new(q, 20);\n"
        "      writeln('birth=', length(q^):1);\n"
        "      q^ := 'first';  writeln('[', q^, ']');\n"
        "      q^ := 'a much longer second'; writeln('[', q^, ']');\n"
        "      dispose(q) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // A freshly allocated string is empty; reading 20 here is the header.
    EXPECT_EQ(R.Stdout, "birth=0\n[first]\n[a much longer second]\n");
}

TEST(Schema, ARunTimeLayoutReachesBelowTheTopLevel) {
    // The run-time address was worked out for `p^`, then for `p^.f`, then for
    // `p^.f[i]` -- each as its own branch, so a component one level deeper fell
    // through to the probe struct and `q^.inner.k` was written into the middle
    // of the string beside it.  Nothing caught it: Sema accepts the program and
    // both size-agreement tripwires stand aside for a varying type.  The whole
    // access path is resolved by one recursion now.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record\n"
        "       inner: record s: string(n); k: integer end;\n"
        "       tail: integer\n"
        "     end;\n"
        "var q: ^t;\n"
        "begin\n"
        "  new(q, 20);\n"
        "  q^.inner.s := 'hello'; q^.inner.k := 7; q^.tail := 9;\n"
        "  writeln(q^.inner.k:1, ' ', q^.tail:1, ' [', q^.inner.s, ']');\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 9 [hello]\n");
}

TEST(Schema, AnArrayOfRecordsInAVaryingBodyStridesAndAddressesCorrectly) {
    // Two extents fixed by the same discriminant, one inside the other: the
    // element stride is a run-time size, and the string capacity inside each
    // element is a run-time capacity reached through the array index.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) =\n"
        "       record a: array[1..n] of record s: string(n); k: integer end end;\n"
        "var q: ^t; i: integer;\n"
        "begin\n"
        "  new(q, 3);\n"
        "  for i := 1 to 3 do begin q^.a[i].s := 'xy'; q^.a[i].k := i * 5 end;\n"
        "  for i := 1 to 3 do write('[', q^.a[i].s, ']', q^.a[i].k:1, ' ');\n"
        "  writeln; dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xy]5 [xy]10 [xy]15 \n");
}

TEST(Schema, IOIntoARunTimeCapacityStringUsesTheRealCapacity) {
    // The capacity bounds how much read and writestr may store, so folding the
    // probe truncated both to a single character.  Only plain assignment asked
    // for the run-time capacity; every other operation on the string still
    // believed the probe.
    auto R = compileAndRun(
        "program p(output);\n"
        "type ps = ^string;\n"
        "var q: ps; s: string(30);\n"
        "begin\n"
        "  new(q, 25);\n"
        "  writestr(q^, 'built ', 42:1, ' here');\n"
        "  writeln('[', q^, '] len=', length(q^):1);\n"
        "  s := q^;\n"
        "  writeln('copied [', s, ']');\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[built 42 here] len=13\ncopied [built 42 here]\n");
}

TEST(Schema, ReadIntoARunTimeCapacityStringDoesNotTruncate) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "type ps = ^string;\n"
        "var q: ps;\n"
        "begin new(q, 25); readln(q^);\n"
        "      writeln('[', q^, '] len=', length(q^):1) end.\n",
        kEP, "hello there world\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello there world] len=17\n");
}

TEST(Schema, EveryStringOperationUsesTheRunTimeCapacity) {
    // Only plain assignment asked for it; substring, substr, concatenation,
    // comparison, index and substring-assignment all folded the probe's
    // string(1), so each of them either truncated or refused a legal program.
    auto R = compileAndRun(
        "program p(output);\n"
        "type ps = ^string;\n"
        "var q, r: ps;\n"
        "begin\n"
        "  new(q, 30); new(r, 30);\n"
        "  q^ := 'hello world'; r^ := 'hello world';\n"
        "  writeln('len=', length(q^):1);\n"
        "  writeln('sub=[', q^[1..5], ']');\n"
        "  writeln('substr=[', substr(q^, 7, 5), ']');\n"
        "  writeln('cat=[', q^ + '!', ']');\n"
        "  writeln('eq=', q^ = r^, ' idx=', index(q^, 'world'):1);\n"
        "  q^[1..5] := 'HELLO';\n"
        "  writeln('after=[', q^, ']');\n"
        "  dispose(q); dispose(r)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "len=11\nsub=[hello]\nsubstr=[world]\ncat=[hello world!]\n"
              "eq=true idx=7\nafter=[HELLO world]\n");
}

TEST(Schema, NewRejectsADiscriminantThatIsNotAUsableExtent) {
    // EP §6.7.5.3 takes the discriminants as expressions, so nothing before
    // run time can tell that one is unusable.  A zero or negative extent sized
    // the allocation from nonsense and put every later access outside it, and
    // was accepted without a word.
    auto R = compileAndRun(
        "program p(output);\n"
        "type ps = ^string;\n"
        "var q: ps; n: integer;\n"
        "begin n := -5; writeln('before'); new(q, n); writeln('after') end.\n",
        kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_EQ(R.Stdout, "before\n");
    EXPECT_NE(R.Stderr.find("not a usable extent"), std::string::npos) << R.Stderr;
}

TEST(Schema, AnArrayLowerBoundOfZeroIsNotAnUnusableExtent) {
    // The check belongs on the EXTENT, not on the discriminants: ExtentVaries
    // is one flag for the whole body and does not say which discriminant sizes
    // anything, so testing them all rejected a legal `array[lo..hi]` whose
    // lower bound is zero.  The commit that added the check claimed it only
    // looked at discriminants that size something; it did not.
    auto R = compileAndRun(
        "program p(output);\n"
        "type vec(lo, hi: integer) = array[lo..hi] of integer;\n"
        "var v: ^vec; i: integer;\n"
        "begin new(v, 0, 4);\n"
        "      for i := 0 to 4 do v^[i] := i * i;\n"
        "      writeln(v^[0]:1, ' ', v^[4]:1); dispose(v) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 16\n");
}

TEST(Schema, ADiscriminantThatFixesNoExtentIsNotRangeChecked) {
    // The check is only for a discriminant that actually sizes something.  A
    // fixed-layout body's discriminant may legitimately be any value, and
    // refusing zero there would reject a program EP allows.
    auto R = compileAndRun(
        "program p(output);\n"
        "type tagged(id: integer) = record count: integer end;\n"
        "var q: ^tagged;\n"
        "begin new(q, 0); q^.count := 7; writeln(q^.count:1, ' ', q^.id:1) end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 0\n");
}

TEST(Schema, WithOverAnUndiscriminatedSchemaPointer) {
    // `with p^ do` was refused outright for a record-bodied schema pointer.
    // The fields are selectable by name like any record's; the difference is
    // that there is no struct to GEP into, so each is bound to the address the
    // run-time layout gives it, and the discriminants to the values the object
    // carries.  A bound string field also has to remember its real capacity:
    // once bound it is an ordinary name with no path back to its object, and
    // it would otherwise be checked against the probe's string(1).
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(cap: integer) = record s: string(cap); n: integer end;\n"
        "var p: ^buf;\n"
        "begin\n"
        "  new(p, 10);\n"
        "  with p^ do begin s := 'hi there'; n := 3 end;\n"
        "  writeln('[', p^.s, '] n=', p^.n:1);\n"
        "  with p^ do writeln('inside [', s, '] n=', n:1, ' cap=', cap:1);\n"
        "  dispose(p)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi there] n=3\ninside [hi there] n=3 cap=10\n");
}

TEST(Schema, AWithBoundComponentKeepsItsRunTimeLayout) {
    // `with p^ do` bound each field to a bare address, which loses the layout
    // for anything reached THROUGH it: an array field was indexed against the
    // probe's 1..1 and a nested record was addressed by the probe struct.  A
    // bound name now resumes the path it was bound from -- the same recursion
    // that resolves `q^.d[i]` written out in full.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record\n"
        "       d: array[1..n] of integer;\n"
        "       inner: record s: string(n); k: integer end\n"
        "     end;\n"
        "var q: ^t; i: integer;\n"
        "begin\n"
        "  new(q, 5);\n"
        "  with q^ do begin\n"
        "    for i := 1 to 5 do d[i] := i * 3;\n"
        "    inner.s := 'five!'; inner.k := 9\n"
        "  end;\n"
        "  with q^ do begin\n"
        "    for i := 1 to 5 do write(d[i]:1, ' ');\n"
        "    writeln('| [', inner.s, '] ', inner.k:1)\n"
        "  end;\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 6 9 12 15 | [five!] 9\n");
}

TEST(Schema, AVaryingExtentInsideAVariantPartIsStillRunTimeLaidOut) {
    // walkVariantFields adds a variant's fields to the record without carrying
    // ExtentVaries up, so a schema whose ONLY varying extent sits in a variant
    // looked fixed and was laid out against the probe.  rtAlignOfTypeNode also
    // skipped the variant part, so the size walk padded to an alignment the
    // align walk did not know about.
    auto R = compileAndRun(
        "program p(output);\n"
        "type buf(n: integer) = record\n"
        "       k: integer;\n"
        "       case tag: boolean of true: (s: string(n)); false: (x: integer)\n"
        "     end;\n"
        "var q: ^buf; canary: integer;\n"
        "begin\n"
        "  canary := 999;\n"
        "  new(q, 20);\n"
        "  q^.k := 5; q^.tag := true; q^.s := 'inside the variant';\n"
        "  writeln(q^.k:1, ' ', q^.tag, ' [', q^.s, '] ', canary:1);\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 true [inside the variant] 999\n");
}

TEST(Schema, AnAccessPathIsWalkedOncePerAssignment) {
    // A string whose capacity a discriminant fixes needs both an address and a
    // capacity, and emitLValue and exprStrCapV each resolved the path from
    // scratch -- so every subscript along the way was emitted twice and a
    // side-effecting one ran twice.  ISO §6.8.2.2 evaluates the variable-access
    // of an assignment once.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record a: array[1..n] of record s: string(n) end end;\n"
        "var q: ^t; calls: integer;\n"
        "function next: integer;\n"
        "begin calls := calls + 1; next := 1 end;\n"
        "begin\n"
        "  calls := 0; new(q, 8);\n"
        "  q^.a[next].s := 'hi';\n"
        "  writeln('next called ', calls:1, ' time(s); [', q^.a[1].s, ']');\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "next called 1 time(s); [hi]\n");
}

TEST(Schema, ASchemaArrayConformsAndPassesItsRealBounds) {
    // Two halves.  Sema refused a schema-bodied array as a conformant actual at
    // all, which made the one way to write a procedure over an undiscriminated
    // schema unavailable; a schema whose body is an array IS an array here.
    // And codegen then passed the bounds off the type, which for such an array
    // are the probe's -- so the callee would have walked 1..1.
    auto R = compileAndRun(
        "program p(output);\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var q: ^vec; i: integer;\n"
        "procedure show(var x: array[lo..hi: integer] of integer);\n"
        "var k: integer;\n"
        "begin\n"
        "  write('lo=', lo:1, ' hi=', hi:1, ':');\n"
        "  for k := lo to hi do write(' ', x[k]:1);\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  new(q, 5);\n"
        "  for i := 1 to 5 do q^[i] := i * 11;\n"
        "  show(q^);\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "lo=1 hi=5: 11 22 33 44 55\n");
}

TEST(Schema, AnElementAlignedOnlyByItsVariantStridesCorrectly) {
    // An array element whose alignment comes only from inside a variant part.
    //
    // Written to cover the variant-part alignment fix, and it does NOT: reverting
    // that fix leaves this passing.  rtVariantSize already aligns the blob to the
    // variant's own alignment, so the record's total is a multiple of it and the
    // trailing pad the fix adds is a no-op.  Kept because the shape -- an array
    // of variant-bearing records in a varying body -- is otherwise uncovered,
    // and labelled so nobody reads it as protecting that line.
    auto R = compileAndRun(
        "program p(output);\n"
        "type rec(n: integer) = array[1..n] of record\n"
        "       c: char;\n"
        "       case tag: boolean of true: (i: integer); false: (j: integer)\n"
        "     end;\n"
        "var q: ^rec; k: integer;\n"
        "begin\n"
        "  new(q, 3);\n"
        "  for k := 1 to 3 do begin\n"
        "    q^[k].c := chr(96+k); q^[k].tag := true; q^[k].i := k*100 end;\n"
        "  for k := 1 to 3 do write(q^[k].c, q^[k].i:1, ' ');\n"
        "  writeln; dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a100 b200 c300 \n");
}

TEST(Schema, ANestedVaryingStringReportsItsCapacityToAReader) {
    // A substring of a string reached below the top level of a varying body.
    //
    // Also written to cover strCapFromPath, and also does not: the substring
    // runtime bounds against the string's LENGTH rather than its capacity, so
    // the source capacity does not change the answer here.  strCapFromPath is a
    // refactor, not a behaviour fix -- what it was extracted for, walking the
    // path once, is covered by AnAccessPathIsWalkedOncePerAssignment.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record inner: record s: string(n) end end;\n"
        "var q: ^t;\n"
        "begin\n"
        "  new(q, 20);\n"
        "  q^.inner.s := 'hello world';\n"
        "  writeln('[', q^.inner.s[1..5], ']');\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello]\n");
}

TEST(Schema, AWithBoundFixedFieldIsNotTreatedAsSchematic) {
    // Binding a with-field recorded its path in VarEntry::schemaTy, which means
    // "this NAME is a schematic object" -- true of `p^`, not of a field of it.
    // So every bound name answered schemaRefOf, and indexing a field whose own
    // layout is FIXED went looking for an array body on the enclosing record and
    // killed the compiler outright on a legal program.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record s: string(n); d: array[1..5] of integer end;\n"
        "var q: ^t; i: integer;\n"
        "begin\n"
        "  new(q, 8);\n"
        "  with q^ do begin\n"
        "    s := 'eight ch';\n"
        "    for i := 1 to 5 do d[i] := i;\n"
        "    writeln(d[3]:1, ' ', s[1])\n"
        "  end;\n"
        "  dispose(q)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 e\n");
}

TEST(Schema, ADiscriminatedInstanceKeepsItsCompileTimeChecks) {
    // ActiveSchemaBindings_ is filled by an ordinary instantiation `t(300)` as
    // well as by the undiscriminated probe, so marking an extent as varying
    // whenever a binding was read marked every DISCRIMINATED instance's fields
    // too -- where the capacity is exactly known.  That silently disabled
    // err_string_too_long and the subrange warning, and capped a string(300) at
    // the 255 that stands in for a capacity plang does not know.  No
    // undiscriminated schema is involved: it was a regression on plain EP.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(m: integer) = record s: string(m) end;\n"
        "var v: t(300); i: integer;\n"
        "begin v.s := 'x';\n"
        "      for i := 1 to 280 do v.s := v.s + 'y';\n"
        "      writeln(length(v.s):1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "281\n");   // 256 while the capacity was treated as unknown

    auto Bad = compileAndRun(
        "program p(output);\n"
        "type t(m: integer) = record s: string(m) end;\n"
        "var v: t(5);\n"
        "begin v.s := 'far longer than five' end.\n", kEP);
    EXPECT_NE(Bad.ExitCode, 0);
    EXPECT_NE(Bad.Stderr.find("does not fit a string(5)"), std::string::npos) << Bad.Stderr;
}

TEST(Schema, AStringIndexOnAPointerToStringIsNotASchemaArray) {
    // `q^[1]` for a `^string` is a string component, §6.5.3.2 -- but the
    // schema-array branch claimed any schema before the string case was
    // reached, went looking for an array body on the string schema, and killed
    // the compiler.  A record-bodied schema has no subscript at all and has to
    // reach a diagnostic rather than the same crash.
    auto R = compileAndRun(
        "program p(output); type ps = ^string; var q: ps;\n"
        "begin new(q, 8); q^ := 'abc'; writeln(q^[1]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a\n");

    auto Rec = compileAndRun(
        "program p(output); type buf(n: integer) = record k: integer end;\n"
        "var p2: ^buf;\n"
        "begin new(p2, 3); writeln(p2^[1]) end.\n", kEP);
    EXPECT_NE(Rec.ExitCode, 0);
    EXPECT_EQ(Rec.Stderr.find("internal error"), std::string::npos) << Rec.Stderr;
    EXPECT_NE(Rec.Stderr.find("non-array"), std::string::npos) << Rec.Stderr;
}

TEST(Schema, AVaryingStringIntoACharArrayChecksItsLength) {
    // §6.4.3.2 wants the lengths equal.  Sema settles that when it knows the
    // capacity and cannot when a discriminant fixes one, so it lets the
    // assignment through -- and copying the array's length out of a shorter
    // string read past the end of the allocation and dropped heap bytes into
    // the array.  A read overrun, introduced by the compatibility rule that
    // made this assignment legal in the first place.
    auto Bad = compileAndRun(
        "program p(output);\n"
        "type ps = ^string;\n"
        "var q: ps; a: packed array[1..40] of char;\n"
        "begin new(q, 4); q^ := 'ab'; a := q^ end.\n", kEP);
    EXPECT_NE(Bad.ExitCode, 0);
    EXPECT_NE(Bad.Stderr.find("cannot fill"), std::string::npos) << Bad.Stderr;

    auto Ok = compileAndRun(
        "program p(output);\n"
        "type ps = ^string;\n"
        "var q: ps; a: packed array[1..4] of char; i: integer;\n"
        "begin new(q, 4); q^ := 'abcd'; a := q^;\n"
        "      write('['); for i := 1 to 4 do write(a[i]); writeln(']') end.\n", kEP);
    ASSERT_EQ(Ok.ExitCode, 0) << Ok.Stderr;
    EXPECT_EQ(Ok.Stdout, "[abcd]\n");
}

TEST(Schema, AValueClauseInsideASchemaBodyIsNotDropped) {
    // EP §6.4.7 with §6.6.  `t(5)` is written as a schema instantiation, and
    // what it denotes is the schema's body -- which neither hasInitialState nor
    // emitInitialState looked through, so every `value` clause inside a schema
    // body was silently ignored and the field began at zero.
    //
    // The varying components are the point of the second half: the body is laid
    // out under THIS instantiation's discriminants, and initializing it through
    // the unbound layout would put the fields at offsets belonging to no
    // instance at all.  s and a either side of k say whether that happened.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record\n"
        "       s: string(n); k: integer value 7;\n"
        "       a: array[1..n] of integer end;\n"
        "var v: t(20); i: integer;\n"
        "begin writeln(v.k:1);\n"
        "      v.s := 'hello'; for i := 1 to 20 do v.a[i] := i;\n"
        "      writeln(v.s, ' ', v.k:1, ' ', v.a[20]:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\nhello 7 20\n");
}

TEST(Schema, TheRunTimeLayoutSurvivesTheOptimizer) {
    // Everything here is emitted as arithmetic over values rather than as
    // constants in a type, and two of the pieces -- a dynamic alloca and the
    // stacksave/stackrestore pair around it -- are exactly the shapes an
    // optimizer is entitled to move.  The suite compiles at the default level
    // only, so nothing else in it would notice.
    const std::string Src =
        "program p(output);\n"
        "type t(n: integer) = record lead: integer; s: string(n);\n"
        "       case tag: boolean of\n"
        "         true:  (c: char;\n"
        "                 case inner: boolean of\n"
        "                    true: (d: real); false: (k: char));\n"
        "         false: (z: integer) end;\n"
        "var q: ^t; v: t(10); r: ^string; i: integer;\n"
        "begin new(q, 10); q^.lead := 111; q^.s := 'ten chars!';\n"
        "      q^.tag := true; q^.c := 'x';\n"
        "      q^.inner := false; q^.k := 'K';\n"
        "      v := q^;\n"
        "      new(r, 300); r^ := '';\n"
        "      for i := 1 to 300 do r^ := r^ + 'x';\n"
        "      writeln('[', v.k, ']', v.lead:1, v.c, ' ',\n"
        "              length(r^):1, ' ', length(trim(r^)):1) end.\n";
    for (const char* O : {"-O0", "-O1", "-O2", "-O3"}) {
        auto R = compileAndRun(Src, kEP + " " + O + " -frange-checks");
        ASSERT_EQ(R.ExitCode, 0) << O << ": " << R.Stderr;
        EXPECT_EQ(R.Stdout, "[K]111x 300 300\n") << O;
    }
}

TEST(Schema, PackAndUnpackCheckTheBoundsTheObjectHas) {
    // ISO §6.7.5.4.  The bounds of a schema array are not in its type -- Sema
    // holds the probe's -- so the check on the starting index was made against
    // "1..-2": one minus the width of z, taken off a probe upper bound of 1.
    // A bound that describes nothing, refusing a legal program.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record a: array[1..n] of char end;\n"
        "var q: ^t; z: packed array[1..4] of char; i: integer;\n"
        "begin new(q, 10);\n"
        "      for i := 1 to 10 do q^.a[i] := chr(ord('a') + i - 1);\n"
        "      pack(q^.a, 3, z); writeln('[', z, ']');\n"
        "      unpack(z, q^.a, 6);\n"
        "      for i := 1 to 10 do write(q^.a[i]); writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[cdef]\nabcdecdefj\n");

    // And the check still refuses an index that really is out of range, now
    // naming the bound the object actually has rather than the probe's.
    auto Bad = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record a: array[1..n] of char end;\n"
        "var q: ^t; z: packed array[1..4] of char;\n"
        "begin new(q, 10); pack(q^.a, 9, z) end.\n", kEP + " -frange-checks");
    EXPECT_NE(Bad.ExitCode, 0);
    EXPECT_NE(Bad.Stderr.find("1..7"), std::string::npos) << Bad.Stderr;
}

TEST(Schema, ANestedVariantSitsWhereBothWalksAgreeItDoes) {
    // The run-time size walk started each alternative at zero and added the
    // offset on afterwards, while the offset walk started at the offset.  Those
    // two are the same number only when the offset is already aligned to the
    // widest field in the part -- which the pre-align guaranteed, and which is
    // not what the STATIC layout does for a NESTED run: layoutVariantCase
    // places nested fields by their own alignment inside the enclosing blob.
    //
    // So `k` sat four bytes past where an ordinary read of it looked.  It takes
    // all three to show: a nested variant, an alternative whose alignment is
    // strictly below the part's widest (char against real), and a copy between
    // `q^` and a discriminated instance, which is where the two layouts meet.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record\n"
        "       lead: integer;\n"
        "       s: string(n);\n"
        "       case tag: boolean of\n"
        "         true:  (c: char;\n"
        "                 case inner: boolean of\n"
        "                    true:  (d: real);\n"
        "                    false: (k: char));\n"
        "         false: (z: integer) end;\n"
        "var q: ^t; v: t(10);\n"
        "begin new(q, 10);\n"
        "      q^.lead := 111; q^.s := 'ten chars!';\n"
        "      q^.tag := true; q^.c := 'x';\n"
        "      q^.inner := false; q^.k := 'K';\n"
        "      v := q^;\n"
        "      writeln('[', q^.k, ']', '[', v.k, ']', v.lead:1, v.c) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[K][K]111x\n");
}

TEST(Schema, AWithOverANestedComponentKeepsTheRunTimeLayout) {
    // `with q^ do` is a Schema and `with q^.inner do` is an ordinary Record
    // that merely lives inside one.  Keying the run-time-layout branch on the
    // type's KIND sent the second to the static path, where the nested
    // string(n) was bound at the probe's capacity of 1: reading a field worked
    // and assigning to one raised "assigned to a string(1)" on legal code.
    //
    // lead and tail bracket the nested record, so a field bound at the wrong
    // offset shows up as one of them changing rather than as a wrong string.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record\n"
        "       lead: integer;\n"
        "       inner: record x: integer; a: array[1..n] of integer;\n"
        "                     s: string(n) end;\n"
        "       tail: integer end;\n"
        "var q: ^t; i: integer;\n"
        "begin new(q, 5); q^.lead := 111; q^.tail := 222;\n"
        "      with q^.inner do begin\n"
        "        x := 9; s := 'five!';\n"
        "        for i := 1 to 5 do a[i] := i * 3 end;\n"
        "      write(q^.inner.x:1, ' ', q^.inner.s, ' ');\n"
        "      for i := 1 to 5 do write(q^.inner.a[i]:1, ' ');\n"
        "      writeln('/ ', q^.lead:1, ' ', q^.tail:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 five! 3 6 9 12 15 / 111 222\n");
}

TEST(Schema, AStringResultIsAsWideAsTheCapacityNobodyKnewYet) {
    // A result temporary needs a size, and for a capacity a discriminant fixes
    // there is no constant to give it -- so exprStrCapStatic's 255 was used,
    // which is the answer for a capacity NOBODY knows.  Here somebody knows it;
    // it is simply not known yet.  Every one of these silently produced a
    // shorter string than the program asked for, on entirely legal code.
    auto R = compileAndRun(
        "program p(output);\n"
        "var q: ^string; s: string(400); i: integer;\n"
        "begin new(q, 400); q^ := '';\n"
        "      for i := 1 to 400 do q^ := q^ + 'y';\n"
        "      writeln('concat ', length(q^):1);\n"
        "      s := substr(q^, 1, 400); writeln('substr ', length(s):1);\n"
        "      s := trim(q^);           writeln('trim   ', length(s):1) end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "concat 400\nsubstr 400\ntrim   400\n");
}

TEST(Schema, ARunTimeSizedTemporaryDoesNotGrowTheStackPerIteration) {
    // The allocation lands where its size is known, which is inside the loop.
    // Without a scope to give the stack back, two million passes take two
    // million pieces of it; with one, the cost is fixed.  The assertion is that
    // this finishes at all -- it does not terminate on a stack that grows.
    auto R = compileAndRun(
        "program p(output);\n"
        "var q: ^string; i: integer;\n"
        "begin new(q, 4000);\n"
        "      for i := 1 to 2000000 do begin q^ := 'abc'; q^ := q^ + 'd' end;\n"
        "      writeln(length(q^):1, ' ', q^) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 abcd\n");
}

TEST(Schema, ADiscriminantIsReadableAndNotWritable) {
    // EP §6.4.7: a discriminant is a value the schematic variable carries, not
    // a component of it.  It is spelled like a field and reads like one, so
    // every way of writing to it was accepted by Sema and then killed codegen
    // with "record has no field named 'n'" -- an internal error on four
    // separate programs that should each have had a diagnostic.
    struct { const char* what; const char* src; const char* wants; } Cases[] = {
        {"assigned through a pointer",
         "begin new(q, 4); q^.n := 5 end.\n",            "not an assignable"},
        {"assigned on an instance",
         "begin v.n := 5 end.\n",                        "not an assignable"},
        {"passed as a var parameter",
         "begin new(q, 4); r(q^.n) end.\n",              "requires a variable"},
        {"assigned inside with",
         "begin new(q, 4); with q^ do n := 99 end.\n",   "not an assignable"},
    };
    for (const auto& C : Cases) {
        const std::string Src =
            "program p(output);\n"
            "type t(n: integer) = record a: array[1..n] of integer end;\n"
            "var q: ^t; v: t(6);\n"
            "procedure r(var x: integer); begin x := 77 end;\n"
            + std::string(C.src);
        auto R = compileAndRun(Src, kEP);
        EXPECT_NE(R.ExitCode, 0) << C.what;
        EXPECT_NE(R.Stderr.find(C.wants), std::string::npos)
            << C.what << ": " << R.Stderr;
    }

    // Reading one is how a program learns how big its own value is, so the
    // rule has to stop at writing.  All three spellings still read.
    auto Ok = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record a: array[1..n] of integer end;\n"
        "var q: ^t; v: t(6);\n"
        "begin new(q, 4);\n"
        "      writeln(q^.n:1); writeln(v.n:1);\n"
        "      with q^ do writeln(n:1) end.\n", kEP);
    ASSERT_EQ(Ok.ExitCode, 0) << Ok.Stderr;
    EXPECT_EQ(Ok.Stdout, "4\n6\n4\n");
}

TEST(Schema, APointerAndAnInstanceOfOneSchemaDoNotShareAnAnnotation) {
    // One declaration serves every instantiation and carries the annotation of
    // whichever Sema resolved LAST, so the run-time layout walk -- which used
    // to ask the node whether its extent varied -- read `^t`'s probe body
    // through `t(20)`'s field types.  It decided the string was fixed and sized
    // it from syntax the discriminants are not bound in: 264 bytes for a
    // 32-byte field, with the rest of the record past the end of it.
    //
    // Declaring the pointer and the instance is the whole test; neither alone
    // reproduces it, because neither alone leaves a foreign annotation behind.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record s: string(n); k: integer end;\n"
        "var q: ^t; v: t(20);\n"
        "begin new(q, 20); q^.s := 'via pointer'; q^.k := 5;\n"
        "      writeln(q^.s, ' / ', q^.k:1);\n"
        "      v.s := 'instance'; v.k := 6;\n"
        "      writeln(v.s, ' / ', v.k:1);\n"
        "      v := q^;\n"
        "      writeln(v.s, ' / ', v.k:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "via pointer / 5\ninstance / 6\nvia pointer / 5\n");
}

TEST(Schema, AWholeValueCopyCarriesAVaryingStringBothWays) {
    // A varying string in the body is the case where the copy length is neither
    // the struct's nor a constant, and where getting it wrong writes past the
    // end of the destination rather than merely producing a wrong string.  k
    // sits behind the string to say where the copy actually stopped.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record s: string(n); k: integer end;\n"
        "var q: ^t; v: t(20); w: t(20);\n"
        "begin new(q, 20);\n"
        "      q^.s := 'a varying body'; q^.k := 42;\n"
        "      v := q^;\n"
        "      writeln(v.s, ' / ', v.k:1);\n"
        "      w.s := 'back the other way'; w.k := 99;\n"
        "      q^ := w;\n"
        "      writeln(q^.s, ' / ', q^.k:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "a varying body / 42\nback the other way / 99\n");
}

TEST(Schema, AWholeValueCopiesEitherWayBetweenAPointerAndAnInstance) {
    // EP §6.4.7.  Only the TARGET being undiscriminated was handled, so both
    // halves of the pair took the compiler down: `v := q^` asked for the LLVM
    // type of a schema, which by construction has none, and `q^ := v` reached
    // for run-time discriminants that a discriminated instance does not carry.
    //
    // The array is what makes this worth testing: a body that really does vary
    // is the only one where the copy length has to come from the discriminants
    // rather than from the struct, and k rides along after it to catch a length
    // that is right for the array and wrong for the record.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record a: array[1..n] of integer; k: integer end;\n"
        "var q: ^t; v: t(4); i: integer;\n"
        "begin new(q, 4);\n"
        "      for i := 1 to 4 do q^.a[i] := i * 10;\n"
        "      q^.k := 77;\n"
        "      v := q^;\n"
        "      for i := 1 to 4 do write(v.a[i]:1, ' ');\n"
        "      writeln(v.k:1);\n"
        "      for i := 1 to 4 do v.a[i] := i;\n"
        "      v.k := 88;\n"
        "      q^ := v;\n"
        "      for i := 1 to 4 do write(q^.a[i]:1, ' ');\n"
        "      writeln(q^.k:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10 20 30 40 77\n1 2 3 4 88\n");
}

TEST(Schema, AWholeValueCopyStillChecksTheDiscriminantsAgree) {
    // The copy length is only right because the two agree, so the check is what
    // makes the memcpy safe rather than a decoration on it.  One side knows its
    // discriminant at compile time and the other at run time, which is exactly
    // the case a compile-time check cannot settle.
    auto R = compileAndRun(
        "program p(output);\n"
        "type t(n: integer) = record k: integer end;\n"
        "var q: ^t; v: t(4);\n"
        "begin new(q, 3); q^.k := 1; v := q^; writeln(v.k:1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("discriminant n differs"), std::string::npos)
        << R.Stderr;
}

TEST(Schema, AnArrayIndexedByANamedTypeVariesWithItsElement) {
    // ISO §6.4.3.2 lets the index be named by its type rather than written as a
    // range, and that branch of array resolution returns early -- so
    // `array[colour] of string(n)` kept the probe's element size and the record
    // was laid out too small.  The named index cannot itself vary; the ELEMENT
    // can, and that is what has to carry up.
    //
    // The fields either side of the array are the point of the test: they are
    // where a stride the run-time walk and the static layout disagree about
    // shows up as corruption rather than as a wrong string.
    auto R = compileAndRun(
        "program p(output);\n"
        "type colour = (red, green, blue);\n"
        "     t(n: integer) = record\n"
        "       lo: integer; a: array[colour] of string(n); hi: integer end;\n"
        "var q: ^t; c: colour;\n"
        "begin new(q, 12); q^.lo := 111; q^.hi := 222;\n"
        "      q^.a[red] := 'scarlet'; q^.a[green] := 'emerald';\n"
        "      q^.a[blue] := 'cobalt';\n"
        "      for c := red to blue do writeln(q^.a[c]);\n"
        "      writeln(q^.lo:1, ' ', q^.hi:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "scarlet\nemerald\ncobalt\n111 222\n");
}

TEST(Schema, ANamedIndexIsTheOnlyVaryingFieldInItsRecord) {
    // The same gap with nothing else in the record to mark it: the body was not
    // marked varying at all and the schema was refused outright.  A named
    // SUBRANGE index as well as an enumeration, since they reach the branch by
    // different routes.
    auto R = compileAndRun(
        "program p(output);\n"
        "type digit = 1..4;\n"
        "     t(n: integer) = record a: array[digit] of string(n) end;\n"
        "var q: ^t; i: digit;\n"
        "begin new(q, 9);\n"
        "      for i := 1 to 4 do q^.a[i] := 'row';\n"
        "      q^.a[3] := 'third';\n"
        "      for i := 1 to 4 do writeln(i:1, ' ', q^.a[i]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 row\n2 row\n3 third\n4 row\n");
}

// ===========================================================================
// EP §6.4.7 differential harness.
//
// One schema body, written once, exercised through every lowering this
// compiler has for it:
//
//   instance   var v: t(K)              a static struct
//   pointer    var q: ^t; new(q, K)     run-time walk, discriminants in a header
//   parameter  procedure b(var v: t)    run-time walk, discriminants as arguments
//
// The essential trick is that writing and reading must CROSS the forms.  A
// lowering that is wrong but self-consistent writes and reads the same wrong
// offsets and looks perfect; the disagreement only becomes observable when one
// form stores a value and a different form loads it.  The first two versions of
// this harness did not cross, and the packed-record and tagless-variant
// defects walked straight through both.
//
// Combinations, each compared against the pure-instance reference:
//   A  write instance  read instance   (reference)
//   B  write pointer   read pointer    (self-consistent: catches size errors)
//   C  write instance  read parameter  (crosses static -> run-time)
//   D  write pointer   copy to instance, read instance  (crosses run-time -> static)
//
// This exists because the fourth review found ten defects that 1,738 tests,
// AddressSanitizer, IR byte-identity and a four-level optimisation sweep were
// all green through.  Those gates measure the ISO 7185 core.
//
// A shape whose defect is still open is marked expectAgree=false and asserted
// to STILL differ, so fixing one fails this test until the shape is promoted.
// ===========================================================================
namespace {

struct SchemaShape {
    const char* name;
    const char* prelude;   // extra type declarations the body needs
    const char* body;      // schema body, after `t(n: integer) = `
    const char* decls;     // extra variables, e.g. "i: integer"
    const char* write;     // statements that fill the object; %s is the object
    const char* read;      // statements that print it; %s is the object
    int         disc;
    bool        expectAgree;
};

std::string subst(const char* Text, const std::string& Obj) {
    std::string Out;
    for (const char* p = Text; *p; ++p) {
        if (p[0] == '%' && p[1] == 's') { Out += Obj; ++p; }
        else Out += *p;
    }
    return Out;
}

// A: instance throughout.  B: pointer throughout.  C: instance stores, a
// schema parameter loads.  D: pointer stores, a whole-value copy hands it to
// an instance which loads.
enum class Combo { A, B, C, D };

RunResult runCombo(const SchemaShape& S, Combo C) {
    const std::string K = std::to_string(S.disc);
    const bool HasDecls = S.decls && *S.decls;
    const std::string DeclTail = HasDecls ? std::string("; ") + S.decls : "";

    std::string Src = "program p(output);\ntype ";
    if (S.prelude && *S.prelude) { Src += S.prelude; Src += "\n     "; }
    Src += "t(n: integer) = "; Src += S.body; Src += ";\n";

    switch (C) {
    case Combo::A:
        Src += "var v: t(" + K + ")" + DeclTail + ";\nbegin\n"
             + subst(S.write, "v") + subst(S.read, "v") + "end.\n";
        break;
    case Combo::B:
        Src += "var q: ^t" + DeclTail + ";\nbegin\n  new(q, " + K + ");\n"
             + subst(S.write, "q^") + subst(S.read, "q^") + "end.\n";
        break;
    case Combo::C:
        Src += "var a: t(" + K + ")" + DeclTail + ";\n";
        Src += "procedure rd(var v: t);\n";
        if (HasDecls) { Src += "var "; Src += S.decls; Src += ";\n"; }
        Src += "begin\n" + subst(S.read, "v") + "end;\n";
        Src += "begin\n" + subst(S.write, "a") + "  rd(a)\nend.\n";
        break;
    case Combo::D:
        Src += "var q: ^t; v: t(" + K + ")" + DeclTail + ";\nbegin\n"
             + "  new(q, " + K + ");\n" + subst(S.write, "q^")
             + "  v := q^;\n" + subst(S.read, "v") + "end.\n";
        break;
    }
    return compileAndRun(Src, kEP);
}

const SchemaShape kShapes[] = {
  { "array-string-trailer", "",
    "record a: array[1..n] of integer; s: string(n); k: integer end",
    "i: integer",
    "  for i := 1 to 6 do %s.a[i] := i * 7;\n  %s.s := 'abcdef'; %s.k := 4242;\n",
    "  for i := 1 to 6 do write(%s.a[i]:1, ' ');\n"
    "  writeln(%s.s, ' ', %s.k:1);\n",
    6, true },

  { "named-ordinal-index", "",
    "record lo: integer; a: array[boolean] of string(n); hi: integer end",
    "",
    "  %s.lo := 11; %s.hi := 22;\n"
    "  %s.a[false] := 'no'; %s.a[true] := 'yes';\n",
    "  writeln(%s.a[false], ' ', %s.a[true], ' ', %s.lo:1, ' ', %s.hi:1);\n",
    5, true },

  { "nested-variant", "",
    "record lead: integer; s: string(n);\n"
    "       case tag: boolean of\n"
    "         true:  (c: char; case inner: boolean of\n"
    "                            true: (d: real); false: (k: char));\n"
    "         false: (z: integer) end",
    "",
    "  %s.lead := 111; %s.s := 'ten chars!';\n"
    "  %s.tag := true; %s.c := 'x'; %s.inner := false; %s.k := 'K';\n",
    "  writeln('[', %s.k, ']', %s.lead:1, %s.c, ' ', %s.s);\n",
    10, true },

  // ---- shapes whose defects review 4 left open ----

  { "nested-instantiation",                       // review-4 finding 1: FIXED
    "inner(m: integer) = array[1..m] of integer;",
    "record a: array[1..n] of integer; x: inner(n); k: integer end",
    "i: integer",
    "  for i := 1 to 4 do begin %s.a[i] := i; %s.x[i] := i * 100 end;\n"
    "  %s.k := 99;\n",
    "  writeln(%s.x[4]:1, ' ', %s.k:1);\n",
    4, true },

  { "tagless-variant", "",                        // review-4 finding 2: FIXED
    "record a: array[1..n] of integer;\n"
    "       case boolean of true: (u: integer); false: (w: char) end",
    "i: integer",
    "  for i := 1 to 2 do %s.a[i] := i * 5;\n  %s.u := 4242;\n",
    "  writeln(%s.a[1]:1, ' ', %s.u:1);\n",
    2, true },

  { "inline-packed-record", "",                   // review-4 finding 3: FIXED
    "record c0: char; p: packed record c: char; x: integer end;\n"
    "       s: string(n) end",
    "",
    "  %s.c0 := 'A'; %s.p.c := 'B'; %s.p.x := 77; %s.s := 'hello';\n",
    "  writeln(%s.c0, ' ', %s.p.c, ' ', %s.p.x:1, ' ', %s.s);\n",
    5, true },

  { "packed-array-element", "",                   // the other half of the flag
    "record c0: char;\n"
    "       a: packed array[1..3] of integer;\n"
    "       s: string(n) end",
    "i: integer",
    "  %s.c0 := 'A'; %s.s := 'hi';\n"
    "  for i := 1 to 3 do %s.a[i] := i * 9;\n",
    "  write(%s.c0, ' ');\n"
    "  for i := 1 to 3 do write(%s.a[i]:1, ' ');\n"
    "  writeln(%s.s);\n",
    4, true },
};

const char* comboName(Combo C) {
    switch (C) {
    case Combo::A: return "instance->instance";
    case Combo::B: return "pointer->pointer";
    case Combo::C: return "instance->parameter";
    case Combo::D: return "pointer->instance (whole-value copy)";
    }
    return "?";
}

} // namespace

TEST(SchemaDifferential, EveryLoweringOfOneTypeAgrees) {
    for (const auto& S : kShapes) {
        const auto Ref = runCombo(S, Combo::A);
        bool AnyDiffers = false;

        for (const Combo C : {Combo::B, Combo::C, Combo::D}) {
            const auto R = runCombo(S, C);
            const bool Differs = R.ExitCode != Ref.ExitCode || R.Stdout != Ref.Stdout;
            AnyDiffers = AnyDiffers || Differs;
            if (!S.expectAgree) continue;

            EXPECT_EQ(Ref.ExitCode, 0) << S.name << " reference: " << Ref.Stderr;
            EXPECT_FALSE(Differs)
                << S.name << ": " << comboName(C) << " disagrees with the "
                << "instance reference.\n"
                << "  reference: [" << Ref.Stdout << "] exit " << Ref.ExitCode << "\n"
                << "  this one : [" << R.Stdout   << "] exit " << R.ExitCode   << "\n"
                << "  stderr   : " << R.Stderr;
        }

        if (!S.expectAgree)
            EXPECT_TRUE(AnyDiffers)
                << S.name << " now agrees across every lowering -- a review-4 defect "
                   "has been fixed.  Set expectAgree = true for this shape.";
    }
}


TEST(SeparateCompilation, AnExportedVariableIsSizedByItsInterfacesConstants) {
    // The interface's `var` denoters are lowered after the module BODY's
    // declarations have been registered, and codegen's constant table is flat:
    // a constant the body declares for its own use displaced the interface's
    // constant of the same spelling, and the exported array was sized from it.
    //
    // Sema never agreed -- the importer resolves tab against the interface --
    // so this surfaced as "takes 16 bytes as it is written and 80 bytes as
    // Sema resolved it", refusing a legal program rather than miscompiling it.
    //
    // Fixed by array bounds coming from Sema rather than being re-folded here,
    // in CodegenTypes.cpp; no module-specific change.  The test is in this file
    // because nothing else would notice if that stopped covering it.
    auto R = compileTwoFiles(
        "module M interface;\n"
        "export M = (n, tab);\n"
        "const n = 10;\n"
        "var tab: array[1..n] of integer;\n"
        "end.\n"
        "module M;\n"
        "const n = 2;\n"
        "var scratch: array[1..n] of integer;\n"
        "to begin do scratch[1] := 0;\n"
        "end.\n",
        "program p(output);\n"
        "import M;\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 1 to 10 do tab[i] := i * 7;\n"
        "  writeln(tab[10]:1)\n"
        "end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "70\n");
}
