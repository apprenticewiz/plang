#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <sys/stat.h>   // mkfifo, for a standard input that stays open
#include <sys/wait.h>   // WIFEXITED, for the exit status of a driver run
#include <unistd.h>
#include <utility>

/// Pins the message language for every child process this file starts.
///
/// These cases run the real binary, which resolves its own catalog from the
/// environment, and ~180 of the assertions below match English message text.
/// Without this, a developer whose shell is set to a language plang ships a
/// catalog for would see those fail for a reason that has nothing to do with
/// what they are testing.  LC_ALL is enough on its own: it is the first thing
/// the resolver reads, and "C" means "no locale", so the child uses the
/// compiled-in English and opens no catalog at all.
///
/// The in-process suites -- scanner, parser, sema, conformance -- need no
/// equivalent: they drive Scanner, Parser and Sema directly and never call
/// selectLocale, so their catalog is empty and every message is English by
/// construction.
const int PinnedLocale = [] {
    ::setenv("LC_ALL", "C", /*overwrite=*/1);
    return 0;
}();

/// A directory of this test's own.  The compiler writes a module's interface
/// file beside its source, under the module's own name, so two tests that use
/// the same module name would otherwise overwrite each other's under `-j`.
static std::string makeTempDir() {
    char Tmpl[] = "/tmp/plang_case_XXXXXX";
    const char* D = mkdtemp(Tmpl);
    return D ? std::string(D) : std::string("/tmp");
}

static void removeTempDir(const std::string& Dir) {
    if (Dir.rfind("/tmp/plang_case_", 0) != 0) return;
    std::error_code Ec;
    std::filesystem::remove_all(Dir, Ec);
}

/// Write \p Content to \p Path.
static bool writeFileAt(const std::string& Path, const std::string& Content) {
    FILE* F = std::fopen(Path.c_str(), "wb");
    if (!F) return false;
    std::fwrite(Content.data(), 1, Content.size(), F);
    std::fclose(F);
    return true;
}

static std::string runCmd(const std::string &Cmd) {
    FILE *Pipe = popen(Cmd.c_str(), "r");
    if (!Pipe) return "";
    std::string Out;
    char Buf[256];
    while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
    pclose(Pipe);
    return Out;
}

/// Run "plang -pc1 <args>" and capture combined stderr output.
static std::string runPC1(const std::string &Args) {
    return runCmd(std::string(PLANG_PATH) + " -pc1 " + Args + " 2>&1");
}

/// Run "plang <args>" and capture combined stderr output.
static std::string runPlang(const std::string &Args) {
    return runCmd(std::string(PLANG_PATH) + " " + Args + " 2>&1");
}

static int lineCount(const std::string &S) {
    if (S.empty()) return 0;
    int N = 0;
    for (char C : S) if (C == '\n') ++N;
    return N;
}

/// How many diagnostics of the given severity the output holds.
///
/// Counting lines will not do: a diagnostic that has a source location is
/// printed over three of them, the message then the offending line then the
/// caret.
static int diagCount(const std::string &S, const std::string &Sev = "error: ") {
    int    N   = 0;
    size_t Pos = 0;
    while ((Pos = S.find(Sev, Pos)) != std::string::npos) { ++N; Pos += Sev.size(); }
    return N;
}

struct RunResult {
    int         ExitCode;
    std::string Stdout;
    std::string Stderr;
};

struct IRResult {
    bool        Ok;     // true if compilation succeeded (exit 0)
    std::string IR;     // LLVM IR text (stdout of plang -emit-llvm)
    std::string Stderr; // compiler diagnostics
};

/// Compile Pascal source to LLVM IR and return it as a string.
/// Uses -pc1 -emit-llvm so no linker or llc is involved.
/// All temp files use mkstemp so parallel test runs don't race.
static IRResult compileAndEmitIR(const std::string &Src,
                                  const std::string &ExtraFlags = "") {
    // A directory of this case's own, for the same reason compileAndRun uses
    // one: a module in the source has its interface written beside it.
    const std::string CaseDir = makeTempDir();
    const std::string SrcPath = CaseDir + "/case.pas";
    writeFileAt(SrcPath, Src);
    const char* SrcTmpl = SrcPath.c_str();

    char OutTmpl[] = "/tmp/plang_irtest_XXXXXX.ll";
    int  OutFd     = mkstemps(OutTmpl, 3);
    close(OutFd);

    char ErrTmpl[] = "/tmp/plang_irtest_XXXXXX.txt";
    int  ErrFd     = mkstemps(ErrTmpl, 4);
    close(ErrFd);

    std::string Cmd = std::string(PLANG_PATH)
        + " -pc1 " + ExtraFlags
        + " -emit-llvm " + SrcTmpl
        + " -o " + OutTmpl
        + " 2>" + ErrTmpl;
    int Rc = std::system(Cmd.c_str());

    IRResult R;
    R.Ok     = (Rc == 0);
    R.IR     = runCmd(std::string("cat ") + OutTmpl);
    R.Stderr = runCmd(std::string("cat ") + ErrTmpl);

    removeTempDir(CaseDir);
    std::remove(OutTmpl);
    std::remove(ErrTmpl);
    return R;
}

/// Returns true if the IR contains all of the given substrings.
static bool irContainsAll(const std::string &IR,
                           std::initializer_list<const char*> Patterns) {
    for (const char* P : Patterns)
        if (IR.find(P) == std::string::npos) return false;
    return true;
}

/// Returns true if the IR contains none of the given substrings.
static bool irContainsNone(const std::string &IR,
                             std::initializer_list<const char*> Patterns) {
    for (const char* P : Patterns)
        if (IR.find(P) != std::string::npos) return false;
    return true;
}

/// Flag selecting Extended Pascal, for the tests that exercise EP-only syntax.
static const std::string kEP = "-std=iso10206";

/// Compile Pascal source through the full pipeline and run the resulting binary.
/// Returns the stdout output, stderr output, and exit code of the program.
/// Cleans up both the source and binary temp files on return.
static RunResult compileAndRun(const std::string &Src,
                               const std::string &ExtraFlags = "",
                               const std::string &StdinText  = "") {
    // Write source into a directory of this case's own: a module in the source
    // has its interface written beside it, under a name this test does not
    // choose, and /tmp is shared with every other case running alongside.
    const std::string CaseDir = makeTempDir();
    const std::string SrcPath = CaseDir + "/case.pas";
    writeFileAt(SrcPath, Src);
    const char* SrcTmpl = SrcPath.c_str();

    // Compile to a temp binary.
    char BinTmpl[] = "/tmp/plang_regtest_XXXXXX";
    int  BinFd     = mkstemp(BinTmpl);
    close(BinFd);

    // Per-invocation stderr file: a fixed path would be raced by ctest -j.
    char ErrTmpl[] = "/tmp/plang_regtest_XXXXXX.err";
    int  ErrFd     = mkstemps(ErrTmpl, 4);
    close(ErrFd);

    // PLANG_TEST_EXTRA_FLAGS re-runs the whole suite under other codegen
    // settings — chiefly -O1/-O2/-O3, since every expected value here is also
    // the answer the optimizer has to preserve.
    const char *EnvFlags = std::getenv("PLANG_TEST_EXTRA_FLAGS");

    std::string CompileCmd = std::string(PLANG_PATH)
        + " " + (EnvFlags ? EnvFlags : "")
        + " " + ExtraFlags
        + " " + SrcTmpl
        + " -o " + BinTmpl
        + " 2>" + ErrTmpl;
    int CompileRc = std::system(CompileCmd.c_str());

    RunResult R;
    R.Stderr = runCmd(std::string("cat ") + ErrTmpl);
    std::remove(ErrTmpl);
    removeTempDir(CaseDir);

    if (CompileRc != 0) {
        R.ExitCode = CompileRc;
        std::remove(BinTmpl);
        return R;
    }

    // Run the binary, capturing stdout, feeding StdinText if supplied.
    std::string InTmplStr;
    std::string Redirect = " < /dev/null";
    if (!StdinText.empty()) {
        char InTmpl[] = "/tmp/plang_regtest_XXXXXX.in";
        int  InFd     = mkstemps(InTmpl, 3);
        write(InFd, StdinText.data(), StdinText.size());
        close(InFd);
        InTmplStr = InTmpl;
        Redirect  = " < " + InTmplStr;
    }

    // Capture the program's own stderr and status: the ISO runtime checks
    // report there, and a test that ignored them could not tell a clean run
    // from an aborted one.
    char RunErrTmpl[] = "/tmp/plang_regtest_XXXXXX.rer";
    int  RunErrFd     = mkstemps(RunErrTmpl, 4);
    close(RunErrFd);

    R.Stdout   = runCmd(std::string(BinTmpl) + Redirect + " 2>" + RunErrTmpl
                        + "; echo \"exit:$?\"");
    R.ExitCode = 0;
    if (auto Pos = R.Stdout.rfind("exit:"); Pos != std::string::npos) {
        R.ExitCode = std::atoi(R.Stdout.c_str() + Pos + 5);
        R.Stdout.erase(Pos);
    }
    R.Stderr += runCmd(std::string("cat ") + RunErrTmpl);
    std::remove(RunErrTmpl);

    if (!InTmplStr.empty()) std::remove(InTmplStr.c_str());
    std::remove(BinTmpl);
    return R;
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
// Regression tests — compile + run (full pipeline)
// ---------------------------------------------------------------------------

// Regression: user-defined type aliases were resolved to ptrTy in codegen,
// causing a segfault at runtime.  The fix adds a typeAliases map that routes
// named types through llvmTypeOfNode() instead of the ptrTy fallback.

TEST(Regression, UserDefinedSubrangeTypeAlias7185) {
    auto R = compileAndRun(
        "program p;\n"
        "type Count = 1..10;\n"
        "var n: Count;\n"
        "begin n := 7; writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(Regression, UserDefinedSubrangeTypeAliasEP) {
    auto R = compileAndRun(
        "program p;\n"
        "const Max = 10;\n"
        "type SmallInt = 1..Max;\n"
        "var x: SmallInt;\n"
        "begin x := 3; writeln(x) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(Regression, UserDefinedSubrangeTypeAliasAssignAndRead) {
    // Exercises both assignment to and reading from an aliased subrange var.
    auto R = compileAndRun(
        "program p;\n"
        "type Pct = 0..100;\n"
        "var a, b: Pct;\n"
        "begin\n"
        "  a := 40;\n"
        "  b := 60;\n"
        "  writeln(a + b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "100\n");
}

// ---------------------------------------------------------------------------
// Tier 4: EP string(N) compile+run regression tests
// ---------------------------------------------------------------------------

TEST(Tier4String, AssignAndWrite) {
    auto R = compileAndRun(
        "program p; var s: string(20);\n"
        "begin s := 'Hello'; writeln(s) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "Hello\n");
}

TEST(Tier4String, CharToString) {
    auto R = compileAndRun(
        "program p; var s: string(10); c: char;\n"
        "begin c := '!'; s := c; writeln(s) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "!\n");
}

TEST(Tier4String, Concatenation) {
    auto R = compileAndRun(
        "program p; var s: string(20); u: string(40);\n"
        "begin s := 'Hello'; u := s + ', World'; writeln(u) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "Hello, World\n");
}

TEST(Tier4String, Length) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Hello'; n := length(s); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Tier4String, Equality) {
    auto R = compileAndRun(
        "program p; var s: string(20); b: boolean;\n"
        "begin s := 'Hello'; b := s = 'Hello'; writeln(b) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(Tier4String, LessThan) {
    auto R = compileAndRun(
        "program p; var s: string(20); b: boolean;\n"
        "begin s := 'Apple'; b := s < 'Banana'; writeln(b) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(Tier4String, SubstrFunction) {
    // EP §6.7.5.4: four characters starting at index 2, not characters 2..4.
    auto R = compileAndRun(
        "program p; var s, u: string(20);\n"
        "begin s := 'Hello'; u := substr(s, 2, 4); writeln(u) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ello\n");
}

TEST(Tier4String, IndexFunction) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Hello'; n := index(s, 'ell'); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(Tier4String, TrimFunction) {
    auto R = compileAndRun(
        "program p; var s, u: string(20); n: integer;\n"
        "begin s := 'hello   '; u := trim(s); n := length(u); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Tier4String, SubstringVariable) {
    auto R = compileAndRun(
        "program p; var s: string(20); n: integer;\n"
        "begin s := 'Pascal'; n := length(s[2..4]); writeln(n) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

// ---------------------------------------------------------------------------
// IR-level tests — verify generated LLVM IR for high-risk codegen paths.
//
// These catch regressions in the Sema→Codegen type-annotation pipeline that
// runtime tests cannot: a wrong function call (e.g. store ptr instead of
// plang_str_from_cstr) may not segfault but silently produces garbage output.
// ---------------------------------------------------------------------------

// --- EP VarString assignment ------------------------------------------------

TEST(IRCodeGen, VarStringAssignLiteral) {
    // s := 'hello' must use plang_str_from_cstr, not a bare store of a ptr.
    auto R = compileAndEmitIR(
        "program p; var s: string(20); begin s := 'hello' end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_cstr"}))
        << "expected plang_str_from_cstr for literal→VarString\n" << R.IR;
    // Guard against the pre-fix bug: storing a raw ptr into the struct.
    EXPECT_TRUE(irContainsNone(R.IR, {"store ptr @"}))
        << "must not store a raw string ptr into a VarString alloca\n" << R.IR;
}

TEST(IRCodeGen, VarStringAssignChar) {
    // s := c  (char variable) must route through plang_str_from_char.
    auto R = compileAndEmitIR(
        "program p; var s: string(10); c: char;\n"
        "begin c := 'x'; s := c end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_char"}))
        << "expected plang_str_from_char for char→VarString\n" << R.IR;
}

TEST(IRCodeGen, VarStringAssignVarString) {
    // s := t  (VarString to VarString) must use plang_str_assign.
    auto R = compileAndEmitIR(
        "program p; var s, t: string(20); begin t := 'hi'; s := t end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_assign"}))
        << "expected plang_str_assign for VarString→VarString\n" << R.IR;
}

// --- EP string concatenation ------------------------------------------------

TEST(IRCodeGen, VarStringConcatStrings) {
    // a + b  (both VarString) must call plang_str_concat.
    auto R = compileAndEmitIR(
        "program p; var a, b, u: string(20);\n"
        "begin a := 'foo'; b := 'bar'; u := a + b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_concat"}))
        << "expected plang_str_concat for VarString+VarString\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {"plang_str_concat_cstr", "plang_str_concat_char"}))
        << "must not use cstr or char variant for VarString+VarString\n" << R.IR;
}

TEST(IRCodeGen, VarStringConcatLiteral) {
    // s + 'literal'  must call plang_str_concat_cstr, not plang_str_concat.
    auto R = compileAndEmitIR(
        "program p; var s, u: string(40);\n"
        "begin s := 'Hello'; u := s + ', World' end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_concat"}))
        << "expected a plang_str_concat variant\n" << R.IR;
}

// --- Short-circuit boolean operators ----------------------------------------

TEST(IRCodeGen, AndThenProducesPhi) {
    // and_then must generate a phi node (not a plain 'and' instruction).
    // If it were eager, there'd be no basic-block split and no phi.
    auto R = compileAndEmitIR(
        "program p; var a, b, c: boolean;\n"
        "begin c := a and_then b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"phi i1"}))
        << "and_then must produce a phi node for short-circuit\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {" and i1 "}))
        << "and_then must not lower to a plain 'and' (eager evaluation)\n" << R.IR;
}

TEST(IRCodeGen, OrElseProducesPhi) {
    auto R = compileAndEmitIR(
        "program p; var a, b, c: boolean;\n"
        "begin c := a or_else b end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"phi i1"}))
        << "or_else must produce a phi node for short-circuit\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {" or i1 "}))
        << "or_else must not lower to a plain 'or'\n" << R.IR;
}

// --- EP string literal materialization --------------------------------------

TEST(IRCodeGen, EPStringLiteralMaterialisesAsStruct) {
    // In EP mode a multi-char string literal used in an expression (not just
    // passed to writeln) must be materialized as a VarString struct alloca,
    // not left as a bare global char array pointer.  This guards the invariant:
    //   exprIsVarStr(e) → emitExpr(e) returns ptr to struct.
    auto R = compileAndEmitIR(
        "program p; var s: string(20); b: boolean;\n"
        "begin b := s = 'hello' end.\n",   // comparison forces literal materialization
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    // plang_str_from_cstr must be called to wrap the literal in a struct.
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_str_from_cstr", "plang_str_eq"}))
        << "EP string literal must be materialized before comparison\n" << R.IR;
}

TEST(IRCodeGen, ISO7185StringLiteralStaysAsPtr) {
    // In iso7185 mode a string literal passed to writeln stays as a bare ptr
    // (no materialization).  There must be no plang_str_from_cstr call.
    auto R = compileAndEmitIR(
        "program p; begin writeln('hello') end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsNone(R.IR, {"plang_str_from_cstr"}))
        << "iso7185 literal must not be materialized as a struct\n" << R.IR;
    EXPECT_TRUE(irContainsAll(R.IR, {"plang_writeln_str"}))
        << "iso7185 literal must go through plang_writeln_str\n" << R.IR;
}

// --- Case range labels ------------------------------------------------------

TEST(IRCodeGen, CaseRangeUsesIfElseChain) {
    // A case with lo..hi range labels must emit icmp sge + icmp sle pairs,
    // not a switch instruction (switch cannot encode ranges).
    auto R = compileAndEmitIR(
        "program p; var i: integer;\n"
        "begin case i of 1..5: writeln; 6..10: writeln end end.\n",
        "-std=iso10206");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_TRUE(irContainsAll(R.IR, {"icmp sge", "icmp sle"}))
        << "range case labels must produce sge/sle comparisons\n" << R.IR;
    EXPECT_TRUE(irContainsNone(R.IR, {"switch i64"}))
        << "range case labels must not use a switch instruction\n" << R.IR;
}

// ---------------------------------------------------------------------------
// Compile-and-run tests for codegen paths previously untested
// ---------------------------------------------------------------------------

// --- Arrays -----------------------------------------------------------------

TEST(CodegenArray, OneBased) {
    // Pascal arrays are 1-based by default; indexing must subtract the lower bound.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  for i := 1 to 5 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(CodegenArray, ArbitraryLowerBound) {
    // a[-2..2] has five elements; writing all must not corrupt adjacent memory.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [-2..2] of integer; i: integer;\n"
        "begin\n"
        "  for i := -2 to 2 do a[i] := i;\n"
        "  for i := -2 to 2 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-2\n-1\n0\n1\n2\n");
}

TEST(CodegenArray, LastElementNoCorruption) {
    // Regression: a[5] in array[1..5] used to overwrite the loop variable.
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  i := 99;\n"
        "  writeln(a[5]);\n"
        "  writeln(i)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "50\n99\n");
}

TEST(CodegenArray, CharArray) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array [1..3] of char;\n"
        "begin\n"
        "  a[1] := 'A'; a[2] := 'B'; a[3] := 'C';\n"
        "  writeln(a[1]); writeln(a[2]); writeln(a[3])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A\nB\nC\n");
}

// --- Records ----------------------------------------------------------------

TEST(CodegenRecord, DirectFieldAccess) {
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var pt: Point;\n"
        "begin\n"
        "  pt.x := 10; pt.y := 20;\n"
        "  writeln(pt.x); writeln(pt.y)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n");
}

TEST(CodegenRecord, ThreeFields) {
    // Each field must land at a distinct struct element.
    auto R = compileAndRun(
        "program p;\n"
        "type Triplet = record a, b, c: integer end;\n"
        "var rec: Triplet;\n"
        "begin\n"
        "  rec.a := 11; rec.b := 22; rec.c := 33;\n"
        "  writeln(rec.a); writeln(rec.b); writeln(rec.c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(CodegenRecord, WithStatement) {
    auto R = compileAndRun(
        "program p;\n"
        "type Pair = record x, y: integer end;\n"
        "var pt: Pair;\n"
        "begin\n"
        "  pt.x := 5; pt.y := 7;\n"
        "  with pt do begin x := x + 1; writeln(x); writeln(y) end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n7\n");
}

// --- Pointers ---------------------------------------------------------------

TEST(CodegenPointer, NewAndDeref) {
    // Regression: new(p) was allocating only 8 bytes regardless of struct size,
    // and p^.field always loaded/stored from offset 0.
    auto R = compileAndRun(
        "program p;\n"
        "type Rec = record a, b, c: integer end;\n"
        "var r: ^Rec;\n"
        "begin\n"
        "  new(r);\n"
        "  r^.a := 11; r^.b := 22; r^.c := 33;\n"
        "  writeln(r^.a); writeln(r^.b); writeln(r^.c);\n"
        "  dispose(r)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(CodegenPointer, IntegerPointer) {
    auto R = compileAndRun(
        "program p;\n"
        "var q: ^integer;\n"
        "begin\n"
        "  new(q);\n"
        "  q^ := 42;\n"
        "  writeln(q^);\n"
        "  dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(CodegenPointer, IndirectFieldMutation) {
    // Mutate a field through a pointer, then read it back through a second pointer
    // aliasing the same heap object — exercises that field GEPs use the correct
    // struct type when the record is accessed via a pointer variable.
    auto R = compileAndRun(
        "program p;\n"
        "type Pair = record first, second: integer end;\n"
        "var q: ^Pair;\n"
        "begin\n"
        "  new(q);\n"
        "  q^.first := 100; q^.second := 200;\n"
        "  writeln(q^.first);\n"
        "  writeln(q^.second);\n"
        "  dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "100\n200\n");
}

TEST(CodegenPointer, SelfReferentialType) {
    // ISO §6.4.4: pointer base-type may be the enclosing record itself.
    // Previously failed with an IR verification error (undefined reference
    // to the unresolved forward type).
    auto R = compileAndRun(
        "program p;\n"
        "type Node = record val: integer; next: ^Node end;\n"
        "var n: ^Node;\n"
        "begin\n"
        "  new(n); n^.val := 42; n^.next := nil;\n"
        "  writeln(n^.val);\n"
        "  dispose(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(CodegenPointer, ForwardPointerReference) {
    // ISO §6.4.4: PNode declared before Node in the same type-definition-part.
    auto R = compileAndRun(
        "program p;\n"
        "type PNode = ^Node;\n"
        "     Node  = record val: integer end;\n"
        "var n: PNode;\n"
        "begin\n"
        "  new(n); n^.val := 99; writeln(n^.val); dispose(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// ---------------------------------------------------------------------------
// Tests derived from the /tmp bug-catching files found during code review.
// These are the exact programs that originally demonstrated each bug.
// ---------------------------------------------------------------------------

TEST(BugReport, ArrayOneBased_Original) {
    // The original reproducer from the code review: array[1..5] must produce
    // 10 20 30 40 50, not 10 20 30 40 <corrupted-loop-var>.
    auto R = compileAndRun(
        "program tarray;\n"
        "var a: array [1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  for i := 1 to 5 do writeln(a[i])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(BugReport, RecordPointerDirectType) {
    // Original ptr_bug.pas: three fields via ^Rec must be independent.
    auto R = compileAndRun(
        "program tptr;\n"
        "type Rec = record a, b, c: integer end;\n"
        "var p: ^Rec;\n"
        "begin\n"
        "  new(p);\n"
        "  p^.a := 11; p^.b := 22; p^.c := 33;\n"
        "  writeln(p^.a); writeln(p^.b); writeln(p^.c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(BugReport, RecordPointerViaTypeAlias) {
    // t_ptr.pas style: pointer accessed through a named type alias (recptr = ^rec).
    // Previously all three fields resolved to offset 0.
    auto R = compileAndRun(
        "program tptr;\n"
        "type\n"
        "  trec = record\n"
        "    a: integer;\n"
        "    b: integer;\n"
        "    c: integer\n"
        "  end;\n"
        "  trecptr = ^trec;\n"
        "var p: trecptr;\n"
        "begin\n"
        "  new(p);\n"
        "  p^.a := 11; p^.b := 22; p^.c := 33;\n"
        "  writeln(p^.a); writeln(p^.b); writeln(p^.c);\n"
        "  dispose(p)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(BugReport, SelfReferentialLinkedList) {
    // The original ll.pas from the code review: type Node = record ... next: ^Node end.
    // Previously produced an IR verification failure; now compiles and runs.
    auto R = compileAndRun(
        "program p;\n"
        "type ListNode = record val: integer; next: ^ListNode end;\n"
        "var head, tail: ^ListNode;\n"
        "begin\n"
        "  new(head); head^.val := 1; head^.next := nil;\n"
        "  new(tail); tail^.val := 2; tail^.next := nil;\n"
        "  head^.next := tail;\n"
        "  writeln(head^.val);\n"
        "  writeln(head^.next^.val)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n2\n");
}

TEST(BugReport, NestedProcedureNoOuterAccess) {
    // Nested procedure that does NOT access outer variables.
    // Previously the call was silently dropped; now it must execute correctly.
    auto R = compileAndRun(
        "program tnested;\n"
        "procedure outer;\n"
        "  var x: integer;\n"
        "  procedure inner;\n"
        "  begin writeln('inner called') end;\n"
        "begin\n"
        "  x := 42;\n"
        "  inner;\n"
        "  writeln(x)\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("inner called"), std::string::npos)
        << "nested call must execute; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("42"), std::string::npos)
        << "outer must complete; stdout: " << R.Stdout;
}

TEST(BugReport, NestedProcedureWithOuterAccess) {
    // Nested procedure that reads and mutates an outer variable via static link.
    // Previously this caused an IR verification failure (cross-function ref).
    auto R = compileAndRun(
        "program tnested;\n"
        "var g: integer;\n"
        "procedure outer;\n"
        "var x: integer;\n"
        "  procedure inner;\n"
        "  begin x := x + 100; writeln('inner ran, x=', x) end;\n"
        "begin\n"
        "  x := 1; inner; writeln('outer x=', x)\n"
        "end;\n"
        "begin g := 0; outer; writeln('done') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("inner ran"), std::string::npos)
        << "nested proc must execute; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("outer x=101"), std::string::npos)
        << "outer must see mutation by inner; stdout: " << R.Stdout;
}

TEST(BugReport, FileIO) {
    // t_file.pas: write to a text file, reset, and read back char by char.
    auto R = compileAndRun(
        "program tfile;\n"
        "var f: text; ch: char;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 'hello');\n"
        "  reset(f);\n"
        "  while not eof(f) do begin read(f, ch); write(ch) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // The space is the line marker: §6.4.3.5 gives f^ that value where a line
    // ends, so reading character by character never yields the newline itself.
    EXPECT_EQ(R.Stdout, "hello \n");
}

// ---------------------------------------------------------------------------
// Internal (unnamed) file regression tests.
//
// rewrite/reset with no file name used to alias the file variable to
// stdout/stdin, so writes landed on the terminal and the read-back saw an
// empty stdin.  BugReport.FileIO above could not catch it: echoing the file
// contents produces the same bytes either way.
// ---------------------------------------------------------------------------

TEST(CodegenFiles, InternalFileRoundTripsValues) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a, b: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 42);\n"
        "  writeln(f, 77);\n"
        "  reset(f);\n"
        "  read(f, a);\n"
        "  read(f, b);\n"
        "  writeln('a=', a, ' b=', b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Exact match: the written values must not also appear on stdout.
    EXPECT_EQ(R.Stdout, "a=42 b=77\n");
}

TEST(CodegenFiles, InternalFileWritesDoNotReachStdout) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  writeln(f, 'secret');\n"
        "  writeln('done')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "done\n");
    EXPECT_EQ(R.Stdout.find("secret"), std::string::npos)
        << "internal file contents leaked to stdout";
}

TEST(CodegenFiles, InternalFileRewriteTruncates) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a: integer;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 11);\n"
        "  rewrite(f); writeln(f, 22);\n"
        "  reset(f); read(f, a);\n"
        "  writeln('a=', a)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=22\n");
}

TEST(CodegenFiles, NamedFileRoundTripsValues) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a: integer;\n"
        "begin\n"
        "  rewrite(f, '/tmp/plang_named_regtest.txt');\n"
        "  writeln(f, 123);\n"
        "  close(f);\n"
        "  reset(f, '/tmp/plang_named_regtest.txt');\n"
        "  read(f, a);\n"
        "  close(f);\n"
        "  writeln('a=', a)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=123\n");
    std::remove("/tmp/plang_named_regtest.txt");
}

// ---------------------------------------------------------------------------
// read/readln into string(N).
//
// These used to dispatch to the integer reader, which parsed nothing and
// stored 0 over the string's length field, leaving the variable empty.
// ---------------------------------------------------------------------------

TEST(CodegenStrings, ReadlnIntoString) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "begin readln(s); writeln('[', s, ']') end.\n",
        kEP, "hello world\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello world]\n");
}

TEST(CodegenStrings, ReadlnAdvancesAcrossMixedTypes) {
    auto R = compileAndRun(
        "program p;\n"
        "var s, t: string(20); n: integer;\n"
        "begin\n"
        "  readln(s); readln(t); readln(n);\n"
        "  writeln('s=', s, ' t=', t, ' n=', n)\n"
        "end.\n",
        kEP, "first line\nsecond\n99\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "s=first line t=second n=99\n");
}

TEST(CodegenStrings, ReadlnTruncatesToCapacity) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(5);\n"
        "begin readln(s); writeln('[', s, '] len=', length(s)) end.\n",
        kEP, "abcdefghij\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcde] len=5\n");
}

// ---------------------------------------------------------------------------
// readln argument handling.
//
// readln used to call the line-consuming reader once per argument, so every
// variable after the first came from a different line, and a leading file
// variable was ignored entirely for the value reads.
// ---------------------------------------------------------------------------

TEST(CodegenStrings, ReadlnTakesAllArgumentsFromOneLine) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin readln(a, b); writeln('a=', a, ' b=', b) end.\n",
        "", "1 2\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=1 b=2\n");
}

TEST(CodegenFiles, ReadlnFromFileVariableUsesThatFile) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; a, b: integer;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 7, ' ', 8); reset(f);\n"
        "  readln(f, a, b);\n"
        "  writeln('a=', a, ' b=', b)\n"
        "end.\n",
        "", "111 222\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Values must come from the file, not from the stdin we also supplied.
    EXPECT_EQ(R.Stdout, "a=7 b=8\n");
}

TEST(CodegenFiles, StandardFileParametersResolve) {
    // program p(input, output) used to reach codegen with no storage for
    // either name and abort with an internal error.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var n: integer;\n"
        "begin\n"
        "  writeln(output, 'via output');\n"
        "  readln(input, n);\n"
        "  writeln('n=', n);\n"
        "  if eof(input) then writeln('eof') else writeln('noeof')\n"
        "end.\n",
        "", "55\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "via output\nn=55\neof\n");
}

TEST(CodegenFiles, RewriteOnOutputKeepsStdout) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin rewrite(output); writeln(output, 'still stdout') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "still stdout\n");
}

TEST(CodegenIO, ReadCharReadsVerbatimIncludingSpace) {
    // ISO §6.9.2: read(ch) for a char variable reads the next character
    // including spaces; it must NOT skip whitespace as scanf(" %c") would.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: char;\n"
        "begin read(a); read(b); read(c);\n"
        "  writeln(ord(a), ' ', ord(b), ' ', ord(c)) end.\n",
        "", "A B");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // ord('A')=65, ord(' ')=32, ord('B')=66
    EXPECT_EQ(R.Stdout, "65 32 66\n");
}

TEST(CodegenIO, ReadCharReadsNewlineVerbatim) {
    // A newline between two chars must be read as the second character,
    // not silently skipped.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: char;\n"
        "begin read(a); read(b);\n"
        "  writeln(ord(a), ' ', ord(b)) end.\n",
        "", "X\nY");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // ord('X')=88, ord('\n')=10
    EXPECT_EQ(R.Stdout, "88 10\n");
}

TEST(CodegenStrings, ReadLeavesLineTerminator) {
    // read (unlike readln) must stop at the terminator without consuming it.
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "begin\n"
        "  read(s);\n"
        "  if eoln then writeln('eoln') else writeln('noeoln');\n"
        "  writeln('[', s, ']')\n"
        "end.\n",
        kEP, "abc\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "eoln\n[abc]\n");
}

// ---------------------------------------------------------------------------
// Nested procedure regression tests (three shapes that previously produced
// LLVM IR verification failures or silently wrong output).
// ---------------------------------------------------------------------------

TEST(CodegenNested, ThreeLevels) {
    // L3 reaching L1's variable via two levels of static link.
    auto R = compileAndRun(
        "program p;\n"
        "procedure L1;\n"
        "  var a: integer;\n"
        "  procedure L2;\n"
        "    procedure L3;\n"
        "    begin a := 99; writeln('L3 set a=', a) end;\n"
        "  begin L3 end;\n"
        "begin\n"
        "  a := 1;\n"
        "  L2;\n"
        "  writeln('L1 a=', a)\n"
        "end;\n"
        "begin L1 end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("L3 set a=99"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("L1 a=99"),     std::string::npos) << R.Stdout;
}

TEST(CodegenNested, RecursionInsideNestedProc) {
    // A nested procedure calling itself recursively (accesses outer depth var).
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var depth: integer;\n"
        "  procedure inner;\n"
        "  begin\n"
        "    depth := depth + 1;\n"
        "    writeln(depth);\n"
        "    if depth < 3 then inner\n"
        "  end;\n"
        "begin\n"
        "  depth := 0;\n"
        "  inner\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("1"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("3"), std::string::npos) << R.Stdout;
}

TEST(CodegenNested, SiblingCallsViaOuter) {
    // Two sibling nested procedures sharing an outer variable (array).
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var arr: array [1..3] of integer;\n"
        "  procedure fill;\n"
        "  var i: integer;\n"
        "  begin for i := 1 to 3 do arr[i] := i * 10 end;\n"
        "  procedure show;\n"
        "  var i: integer;\n"
        "  begin for i := 1 to 3 do writeln(arr[i]) end;\n"
        "begin\n"
        "  fill;\n"
        "  show\n"
        "end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n");
}

// ---------------------------------------------------------------------------
// Forward declaration regression test.
// ---------------------------------------------------------------------------

TEST(Regression, ForwardDeclarationSignatureMismatch) {
    // A mismatch between forward-declaration and implementation parameter types
    // must be caught by Sema, not escape to produce an IR verification failure.
    auto R = compileAndRun(
        "program p;\n"
        "procedure foo(x: integer); forward;\n"
        "procedure foo(x: real);\n"
        "begin writeln(x) end;\n"
        "begin foo(1) end.\n");
    EXPECT_NE(R.ExitCode, 0)
        << "forward-decl param type mismatch must be rejected";
}

// ---------------------------------------------------------------------------
// File variable struct-size regression test.
// ---------------------------------------------------------------------------

TEST(Regression, FileVariableDoesNotCorruptAdjacentStack) {
    // PascalFile { FILE*, int } is 16 bytes; codegen previously allocated only
    // 8 (alloca ptr), so plang_reset writing F->Buf at offset 8 corrupted the
    // adjacent stack slot.  Guard variables on either side must be untouched.
    auto R = compileAndRun(
        "program p;\n"
        "var guard1: integer; f: text; guard2: integer;\n"
        "begin\n"
        "  guard1 := 111111; guard2 := 222222;\n"
        "  rewrite(f);\n"
        "  writeln(f, 'hello');\n"
        "  reset(f);\n"
        "  writeln(guard1);\n"
        "  writeln(guard2)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("111111"), std::string::npos)
        << "guard1 corrupted; stdout: " << R.Stdout;
    EXPECT_NE(R.Stdout.find("222222"), std::string::npos)
        << "guard2 corrupted; stdout: " << R.Stdout;
}

// ---------------------------------------------------------------------------
// Program-level variables must have exactly one storage location.  main() used
// to alloca its own copy of every global, so a procedure writing a global and
// the program body reading it referred to different memory.
// ---------------------------------------------------------------------------

TEST(CodegenGlobals, ProcedureWriteIsVisibleToProgramBody) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure setIt;\n"
        "begin g := 42 end;\n"
        "begin\n"
        "  g := 0;\n"
        "  setIt;\n"
        "  writeln('g=', g)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("g=42"), std::string::npos)
        << "global write from procedure lost; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, ProgramBodyWriteIsVisibleToProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure showIt;\n"
        "begin writeln('seen=', g) end;\n"
        "begin\n"
        "  g := 7;\n"
        "  showIt\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("seen=7"), std::string::npos)
        << "program-body write not seen by procedure; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, NestedProcedureSharesGlobalWithProgramBody) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "procedure outer;\n"
        "  procedure inner;\n"
        "  begin g := g + 100 end;\n"
        "begin inner end;\n"
        "begin\n"
        "  g := 1;\n"
        "  outer;\n"
        "  writeln('g=', g)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("g=101"), std::string::npos)
        << "global not shared with nested procedure; stdout: " << R.Stdout;
}

TEST(CodegenGlobals, StringGlobalIsInitializedAndShared) {
    // main no longer allocas globals, so plang_str_init must still run against
    // the global storage for string(N) program variables.
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20); n: integer;\n"
        "procedure fill;\n"
        "begin s := 'hello'; n := length(s) end;\n"
        "begin\n"
        "  n := 0;\n"
        "  fill;\n"
        "  writeln('s=', s);\n"
        "  writeln('n=', n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_NE(R.Stdout.find("s=hello"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stdout.find("n=5"),     std::string::npos) << R.Stdout;
}

// ---------------------------------------------------------------------------
// ISO set operators.
//
// The union/difference/intersection lowering and the runtime helpers both
// existed, but Sema rejected the operators before codegen ever ran, and the
// inclusion relations fell through to a signed compare on the bitmask.
// ---------------------------------------------------------------------------

TEST(CodegenSets, ArithmeticOperators) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: set of 1..10; i: integer;\n"
        "procedure show(s: set of 1..10);\n"
        "  var j: integer;\n"
        "begin\n"
        "  for j := 1 to 10 do if j in s then write(j);\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  a := [1,2,3,4]; b := [3,4,5,6];\n"
        "  c := a + b; show(c);\n"
        "  c := a * b; show(c);\n"
        "  c := a - b; show(c)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "123456\n34\n12\n");
}

TEST(CodegenSets, InclusionRelations) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: set of 1..10;\n"
        "begin\n"
        "  a := [1,2,3]; b := [1,2,3,4,5];\n"
        "  writeln(a <= b, ' ', b <= a, ' ', b >= a, ' ', a = a, ' ', a <> b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false true true true\n");
}

// Sets hold one bit per ordinal across the full base-type width.  These cover
// ordinals above 63, which an earlier uint64 representation dropped silently.

TEST(CodegenSets, CharacterSetMembership) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "begin\n"
        "  s := ['a', 'b', 'z'];\n"
        "  writeln('a' in s, ' ', 'z' in s, ' ', 'q' in s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true false\n");
}

TEST(CodegenSets, CharacterRangeIdiom) {
    auto R = compileAndRun(
        "program p;\n"
        "var ch: char; n: integer;\n"
        "begin\n"
        "  n := 0;\n"
        "  for ch := 'a' to 'z' do\n"
        "    if ch in ['a', 'e', 'i', 'o', 'u'] then n := n + 1;\n"
        "  writeln(n)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(CodegenSets, SubrangeRangeConstructorAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..255; i, n: integer;\n"
        "begin\n"
        "  s := [10..20, 200..210];\n"
        "  n := 0;\n"
        "  for i := 0 to 255 do if i in s then n := n + 1;\n"
        "  writeln(n, ' ', 205 in s, ' ', 100 in s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22 true false\n");
}

TEST(CodegenSets, OperatorsAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: set of 0..255;\n"
        "begin\n"
        "  a := [64, 100, 200]; b := [100, 200, 255];\n"
        "  writeln(card(a + b), ' ', card(a * b), ' ', card(a - b));\n"
        "  writeln(64 in (a + b), ' ', 64 in (a * b), ' ', 255 in (a - b))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 2 1\ntrue false false\n");
}

TEST(CodegenSets, CardinalityAtFullWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "begin\n"
        "  s := [chr(0)..chr(255)];\n"
        "  writeln(card(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "256\n");
}

// The loop variable takes the set's element type, so a set of char yields
// characters rather than their ordinals.
TEST(CodegenSets, ForInOverCharacterSet) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char; c: char;\n"
        "begin\n"
        "  s := ['a', 'b', 'z'];\n"
        "  for c in s do write(c);\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abz\n");
}

TEST(CodegenSets, ForInAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..255; i: integer;\n"
        "begin\n"
        "  s := [3, 70, 200];\n"
        "  for i in s do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 70 200 \n");
}

TEST(CodegenSets, SymmetricDifferenceAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b, c: set of 0..255; i: integer;\n"
        "begin\n"
        "  a := [1, 100]; b := [100, 250];\n"
        "  c := a >< b;\n"
        "  for i in c do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 250 \n");
}

// Sets cross procedure boundaries by value at their full width.
TEST(CodegenSets, PassedAsParameterAboveWordWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of char;\n"
        "function count(t: set of char): integer;\n"
        "begin count := card(t) end;\n"
        "begin\n"
        "  s := ['a'..'e', 'x'];\n"
        "  writeln(count(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

TEST(SemaSets, MixedOperandsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10; i: integer;\n"
        "begin s := [1]; i := 3; i := s + i end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot mix set and non-set"), std::string::npos)
        << R.Stderr;
}

TEST(SemaSets, OrderingOperatorsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10;\n"
        "begin s := [1]; if s < s then writeln('x') end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not defined for sets"), std::string::npos) << R.Stderr;
}

// ISO §6.8.2.2: a parameterless function-identifier in an expression is a
// call.  Codegen used to treat the bare name as an out-of-scope variable and
// emit a reference to a global "g_pick" that nothing defines, so the program
// failed at link time rather than running.
TEST(CodegenProcs, ParameterlessFunctionIdentifierIsACall) {
    auto R = compileAndRun(
        "program p;\n"
        "var g: integer;\n"
        "function pick: integer;\n"
        "begin pick := 7 end;\n"
        "procedure show(v: integer); begin writeln(v) end;\n"
        "begin g := pick; writeln(g); show(pick); writeln(pick + 1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n8\n");
}

// The call still needs the static link, so it cannot bypass the call path.
TEST(CodegenProcs, ParameterlessNestedFunctionSeesEnclosingLocals) {
    auto R = compileAndRun(
        "program p;\n"
        "function outer(n: integer): integer;\n"
        "  var acc: integer;\n"
        "  function bump: integer;\n"
        "  begin bump := acc + 1 end;\n"
        "begin acc := n; outer := bump + bump end;\n"
        "begin writeln(outer(10)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "22\n");
}

// ---------------------------------------------------------------------------
// Sets over a base type that reaches below zero (ISO §6.4.3.4).  Bit 0 of the
// mask stands for the base type's lower bound rather than for ordinal 0, so
// every ordinal has to be rebased on the way in and back on the way out.
// ---------------------------------------------------------------------------

TEST(CodegenSets, NegativeBaseMembershipAndCardinality) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -5..10;\n"
        "begin\n"
        "  s := [-5, -1, 0, 3];\n"
        "  writeln(card(s), ' ', -5 in s, ' ', -1 in s, ' ', -2 in s, ' ', 3 in s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 true true false true\n");
}

TEST(CodegenSets, NegativeBaseRangeConstructor) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -5..10; i: integer;\n"
        "begin\n"
        "  s := [-3 .. 1];\n"
        "  for i in s do write(i, ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-3 -2 -1 0 1 \n");
}

TEST(CodegenSets, NegativeBaseOperators) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -8..8; s = set of r;\n"
        "var a, b: s; i: r;\n"
        "begin\n"
        "  a := [-8, -1, 2]; b := [-1, 2, 8];\n"
        "  for i in a + b do write(i, ' '); writeln;\n"
        "  for i in a * b do write(i, ' '); writeln;\n"
        "  for i in a - b do write(i, ' '); writeln;\n"
        "  for i in a >< b do write(i, ' '); writeln;\n"
        "  writeln(a <= a + b, ' ', a = b)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-8 -1 2 8 \n-1 2 \n-8 \n-8 8 \ntrue false\n");
}

// The window is 256 ordinals wide wherever it starts, so a base type ending
// below zero is as representable as one starting at zero.
TEST(CodegenSets, NegativeBaseAtFullWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -256..-1;\n"
        "begin\n"
        "  s := [-256 .. -1];\n"
        "  writeln(card(s), ' ', -256 in s, ' ', -1 in s)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "256 true true\n");
}

TEST(SemaSets, BaseSpanningMoreThanTheLimitRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of -300..0;\n"
        "begin s := [] end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("exceeds the 256-element limit"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// A set-constructor has no type of its own; the context supplies one, which is
// what tells codegen where to put ordinal -1.
// ---------------------------------------------------------------------------

TEST(CodegenSets, ConstructorTakesItsTypeFromTheContext) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = -5..10; s = set of r;\n"
        "var g: s; i: r;\n"
        "function pick(n: integer): s;\n"
        "begin pick := [-4, 6] end;\n"
        "procedure show(v: s);\n"
        "  var k: r;\n"
        "begin for k in v do write(k, ' '); writeln end;\n"
        "begin\n"
        "  show([-2, 0, 4]);\n"           // argument
        "  show(pick(0));\n"              // function result
        "  g := [-1] + [3];\n"            // constructor on both sides of '+'
        "  show(g);\n"
        "  show(g + [-5]);\n"             // one loose operand, one typed
        "  i := -1;\n"
        "  writeln(i in [-5 .. -1])\n"    // 'in' takes its window from i
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-2 0 4 \n-4 6 \n-1 3 \n-5 -1 3 \ntrue\n");
}

// With no context at all the constructor still has to hold its own elements,
// so it reads a window off them.
TEST(CodegenSets, ConstructorWithoutContextSpansItsOwnElements) {
    auto R = compileAndRun(
        "program p;\n"
        "begin\n"
        "  writeln(card([-4, -2, 7]), ' ', -4 in [-4, -2, 7], ' ', -3 in [-4, -2, 7])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 true false\n");
}

// Set types used to be interned by their element type's *name*, and every
// anonymous subrange is named "subrange", so these two were the same type.
// They are two, and being two they are based at different ordinals: bit 0 of
// the first stands for -5 and bit 0 of the second for 0.
//
// ISO §6.4.5 c) makes them compatible all the same, their base types being
// subranges of the one host type, so a value crosses between them and has to
// be moved five places on the way.  It used to be carried across unmoved, and
// {1} arrived as {-4}.
TEST(SemaSets, AValueCrossingSetWindowsKeepsItsMembers) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var x: a; y: b; i: integer;\n"
        "begin\n"
        "  y := [1, 3];\n"
        "  x := y;\n"
        "  for i := -5 to 10 do if i in x then write(i:3);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  1  3\n");
}

// The operands of a set operator meet in one window too, and a comparison
// takes the lower of the two origins, where neither operand loses a bit.
TEST(SemaSets, OperandsOfDifferentWindowsMeetInOne) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var x: a; y: b; i: integer;\n"
        "begin\n"
        "  x := [-3]; y := [1, 3];\n"
        "  for i := -5 to 10 do if i in (x + y) then write(i:3);\n"
        "  writeln;\n"
        "  if x = y then writeln('same') else writeln('differ');\n"
        "  x := [1]; y := [1];\n"
        "  if x = y then writeln('same') else writeln('differ')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " -3  1  3\ndiffer\nsame\n");
}

// A value parameter is a window crossing as much as an assignment is, and the
// callee's window is the one recorded for its parameter.
TEST(SemaSets, ASetArgumentArrivesInTheParametersWindow) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type a = set of -5..10; b = set of 0..10;\n"
        "var y: b;\n"
        "procedure show(s: a);\n"
        "var j: integer;\n"
        "begin\n"
        "  for j := -5 to 10 do if j in s then write(j:3);\n"
        "  writeln\n"
        "end;\n"
        "begin y := [1, 3]; show(y) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "  1  3\n");
}

// ---------------------------------------------------------------------------
// Constant bounds
//
// constBound used to answer with the expression's own address when it could
// not fold, which reads as an ordinary bound at any call site that forgets to
// test for it.  Two forgot, so `array[1..n]` over a variable `n` was accepted,
// lowered to [0 x i64], and segfaulted on the first subscript — index checks
// could not fire because the bounds they compare against were pointer bits.
// An inverted constant range reached the same [0 x i64] by a different route.
// ---------------------------------------------------------------------------

TEST(ConstBound, ArrayOverAVariableBoundIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var n: integer;\n"
        "    a: array[1..n] of integer;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("not a constant expression"), std::string::npos)
        << R.Stderr;
    // The whole point: no zero-element object escapes to be indexed.
    EXPECT_EQ(R.IR.find("[0 x "), std::string::npos) << R.IR;
}

TEST(ConstBound, BothVariableBoundsAreReported) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var lo, hi: integer;\n"
        "    a: array[lo..hi] of integer;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("lower bound"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("upper bound"), std::string::npos) << R.Stderr;
}

TEST(ConstBound, ArrayBoundDiagnosticSuggestsASchemaType) {
    auto R = compileAndEmitIR(
        "program p;\nvar n: integer;\n"
        "    a: array[1..n] of integer;\nbegin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("schema type"), std::string::npos) << R.Stderr;
}

TEST(ConstBound, SubrangeOverAVariableBoundIsRejected) {
    // A type can only see a variable from an enclosing scope, so the
    // declaration has to be nested to be written at all.
    auto R = compileAndEmitIR(
        "program p;\n"
        "var n: integer;\n"
        "procedure q;\n"
        "type s = 1..n;\n"
        "var x: s;\n"
        "begin end;\n"
        "begin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("upper bound of subrange"), std::string::npos)
        << R.Stderr;
}

TEST(ConstBound, InvertedConstantRangeIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\nvar a: array[5..1] of integer;\nbegin end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("exceeds upper bound"), std::string::npos)
        << R.Stderr;
    EXPECT_EQ(R.IR.find("[0 x "), std::string::npos) << R.IR;
}

TEST(ConstBound, InvertedBoundsReadBackAsWritten) {
    auto Chars = compileAndEmitIR(
        "program p;\nvar a: array['z'..'a'] of integer;\nbegin end.\n");
    EXPECT_FALSE(Chars.Ok);
    EXPECT_NE(Chars.Stderr.find("'z' exceeds upper bound 'a'"),
              std::string::npos) << Chars.Stderr;

    auto Enums = compileAndEmitIR(
        "program p;\ntype c = (r, g, b);\n"
        "var a: array[b..r] of integer;\nbegin end.\n");
    EXPECT_FALSE(Enums.Ok);
    EXPECT_NE(Enums.Stderr.find("b exceeds upper bound r"), std::string::npos)
        << Enums.Stderr;
}

TEST(ConstBound, ConstantFoldedBoundsStillWork) {
    auto R = compileAndRun(
        "program p;\n"
        "const k = 4;\n"
        "var a: array[1..k*2] of integer; i: integer;\n"
        "begin for i := 1 to 8 do a[i] := i * i; writeln(a[8]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "64\n");
}

TEST(ConstBound, NegativeAndCharAndEnumBoundsStillWork) {
    auto R = compileAndRun(
        "program p;\n"
        "type c = (red, green, blue);\n"
        "var a: array[-3..-1] of integer;\n"
        "    b: array['a'..'e'] of integer;\n"
        "    d: array[red..blue] of integer;\n"
        "begin\n"
        "  a[-2] := 7; b['c'] := 8; d[green] := 9;\n"
        "  writeln(a[-2] + b['c'] + d[green])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "24\n");
}

TEST(ConstBound, SchemaTypesStillTakeARunTimeLength) {
    // The diagnostic points here, so this had better keep working.
    auto R = compileAndRun(
        "program p;\n"
        "type vec(n: integer) = array[1..n] of integer;\n"
        "var v: vec(4); i: integer;\n"
        "begin for i := 1 to 4 do v[i] := i * 10; writeln(v[4]) end.\n",
        "-std=iso10206");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "40\n");
}

TEST(ConstBound, StringCapacityMustBeConstantAndPositive) {
    auto Var = compileAndEmitIR(
        "program p;\nvar n: integer; s: string(n);\nbegin end.\n",
        "-std=iso10206");
    EXPECT_FALSE(Var.Ok);
    EXPECT_NE(Var.Stderr.find("constant expression"), std::string::npos)
        << Var.Stderr;

    // This silently became string(255), so the declared capacity meant nothing.
    auto Zero = compileAndEmitIR(
        "program p;\nvar s: string(0);\nbegin end.\n", "-std=iso10206");
    EXPECT_FALSE(Zero.Ok);
    EXPECT_NE(Zero.Stderr.find("must be positive"), std::string::npos)
        << Zero.Stderr;
}

TEST(ConstBound, EmptySetLiteralRangeIsStillLegal) {
    // ISO §6.7.1: in a set, a range whose first value exceeds its second
    // denotes no values.  Only *types* need an ascending range.
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..10;\n"
        "begin s := [5..1]; if s = [] then writeln('empty') "
        "else writeln('not empty') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "empty\n");
}

// ---------------------------------------------------------------------------
// Optimization pipeline
//
// -O used to reach only llc, so the LLVM middle end never ran and every
// variable stayed a stack slot.  Turning it on also made the missing data
// layout matter: without one, LLVM's defaults align i64 to four bytes and lay
// records out differently from the machine the backend targets, so the two
// disagreed about where a field was.
// ---------------------------------------------------------------------------

TEST(CodegenOpt, ModuleCarriesTheTargetDataLayout) {
    auto R = compileAndEmitIR("program p;\nbegin end.\n");
    ASSERT_TRUE(R.Ok) << R.Stderr;
    EXPECT_NE(R.IR.find("target datalayout"), std::string::npos) << R.IR;
    // The default layout omits this, and that is exactly the difference that
    // moved a record field out from under the backend.
    EXPECT_NE(R.IR.find("i64:64"), std::string::npos) << R.IR;
}

TEST(CodegenOpt, OptimizationPromotesLocalsOutOfMemory) {
    const char *Src =
        "program p;\n"
        "function work(n: integer): integer;\n"
        "var i, acc: integer;\n"
        "begin acc := 0; for i := 1 to n do acc := acc + i; work := acc end;\n"
        "begin writeln(work(10)) end.\n";

    auto Unopt = compileAndEmitIR(Src, "-O0");
    auto Opt   = compileAndEmitIR(Src, "-O2");
    ASSERT_TRUE(Unopt.Ok) << Unopt.Stderr;
    ASSERT_TRUE(Opt.Ok) << Opt.Stderr;

    auto count = [](const std::string &S, const std::string &Needle) {
        size_t N = 0;
        for (size_t P = S.find(Needle); P != std::string::npos;
             P = S.find(Needle, P + 1)) ++N;
        return N;
    };
    EXPECT_GT(count(Unopt.IR, "alloca"), 0u);
    EXPECT_EQ(count(Opt.IR, "alloca"), 0u) << Opt.IR;
}

TEST(CodegenOpt, OptimizedRecordFieldsSurviveARuntimeRoundTrip) {
    // The layout disagreement showed up here first: the runtime filled a
    // TimeStamp and the optimized read of a later field came back wrong.
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  if t.DateValid then writeln('date ok') else writeln('date bad');\n"
        "  if t.TimeValid then writeln('time ok') else writeln('time bad')\n"
        "end.\n",
        "-std=iso10206 -O2");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "date ok\ntime ok\n");
}

TEST(CodegenOpt, EveryLevelComputesTheSameAnswer) {
    const char *Src =
        "program p;\n"
        "var i, j, acc: integer;\n"
        "begin\n"
        "  acc := 0;\n"
        "  for i := 1 to 40 do for j := 1 to 40 do acc := acc + (i mod 7) * (j mod 3);\n"
        "  writeln(acc)\n"
        "end.\n";
    const std::string Expected = compileAndRun(Src, "-O0").Stdout;
    ASSERT_FALSE(Expected.empty());
    for (const char *Flag : {"-O1", "-O2", "-O3"}) {
        auto R = compileAndRun(Src, Flag);
        EXPECT_EQ(R.ExitCode, 0) << Flag << ": " << R.Stderr;
        EXPECT_EQ(R.Stdout, Expected) << Flag;
    }
}

// ---------------------------------------------------------------------------
// Type identity
//
// Structural types are interned so that two spellings of one type are one
// object, and nominal types (enumerations, records) are one object per
// declaration.  Both halves used to be keyed on the display name, which is not
// an identity: every anonymous record is described the same way, so records
// were all mutually compatible, and pointers to distinct enumerations were the
// same type.  Naming a type hid all of it, which is why these tests spell the
// types out.
// ---------------------------------------------------------------------------

TEST(SemaTypeIdentity, TextIsOneTypeEverywhere) {
    // ISO §6.4.3.5: text is predefined, so a text argument matches a text
    // parameter.  This used to fail with "expected 'text', got 'text'".
    auto R = compileAndRun(
        "program p;\n"
        "var f: text;\n"
        "procedure w(var t: text);\n"
        "begin writeln(t, 'ok') end;\n"
        "begin rewrite(f); w(f) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, StructuralFileTypesMatchAcrossSpellings) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "procedure w(var g: file of integer);\n"
        "begin rewrite(g); write(g, 7) end;\n"
        "begin w(f) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, StringCapacityTypesMatchAcrossSpellings) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure w(var t: string(20));\n"
        "begin t := 'ok' end;\n"
        "begin w(s); writeln(s) end.\n",
        "-std=iso10206");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ok\n");
}

TEST(SemaTypeIdentity, PointersToDistinctEnumsAreDistinct) {
    // Both pointees were described "(enum)", so the pointer cache returned one
    // type for both and this assignment was accepted.
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (red, green); c2 = (blue, white);\n"
        "var a: ^c1; b: ^c2;\n"
        "begin new(a); new(b); a := b end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'^c1'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'^c2'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, PointersToDistinctAnonRecordsAreDistinct) {
    auto R = compileAndRun(
        "program p;\n"
        "type p1 = ^record a: integer end;\n"
        "     p2 = ^record b, c, d: integer end;\n"
        "var x: p1; y: p2;\n"
        "begin new(x); new(y); x := y end.\n");
    EXPECT_NE(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, DistinctEnumTypesAreNotCompatible) {
    // Every enumeration was named "(enum)", so every one was compatible with
    // every other (ISO §6.4.2.3 makes each definition a distinct type).
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (aa, bb); c2 = (dd, ee);\n"
        "var u: c1; v: c2;\n"
        "begin u := aa; v := dd; u := v end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'c1'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'c2'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, DistinctRecordTypesAreNotCompatible) {
    // Likewise every record was named "(record)", so records of entirely
    // different shapes were assignable to each other.
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end;\n"
        "     b = record y: real; z: char end;\n"
        "var s: a; t: b;\n"
        "begin s := t end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("'a'"), std::string::npos) << R.Stderr;
    EXPECT_NE(R.Stderr.find("'b'"), std::string::npos) << R.Stderr;
}

TEST(SemaTypeIdentity, SubrangesOverDistinctEnumsAreDistinct) {
    // The subrange cache keyed on the base's name, and both bases were
    // "(enum)" with the same ordinal bounds, so the two collided.
    auto R = compileAndRun(
        "program p;\n"
        "type c1 = (aa, bb, cc); c2 = (dd, ee, ff);\n"
        "     s1 = aa..cc; s2 = dd..ff;\n"
        "var u: s1; v: s2;\n"
        "begin u := aa; v := dd; u := v end.\n");
    EXPECT_NE(R.ExitCode, 0) << R.Stderr;
}

TEST(SemaTypeIdentity, SameRecordTypeStillAssignable) {
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end;\n"
        "var s, t: a;\n"
        "begin t.x := 9; s := t; writeln(s.x) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9\n");
}

TEST(SemaTypeIdentity, TypeAliasKeepsTheAliasedTypesIdentity) {
    // `b = a` must not rename a's type, and a value of one must still pass as
    // the other, since they are the same type.
    auto R = compileAndRun(
        "program p;\n"
        "type a = record x: integer end; b = a;\n"
        "var s: a; t: b;\n"
        "procedure w(var q: a);\n"
        "begin q.x := 4 end;\n"
        "begin w(t); s := t; writeln(s.x) end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4\n");
}

TEST(SemaTypeIdentity, ForwardPointerIsTheSameTypeAsALaterMention) {
    // A forward `^node` is interned under a placeholder; if resolving it did
    // not re-file it, the `^node` in the var declaration would be a second,
    // incompatible pointer type.
    auto R = compileAndRun(
        "program p;\n"
        "type pnode = ^node;\n"
        "     node = record v: integer; next: ^node end;\n"
        "var head: pnode; other: ^node;\n"
        "begin\n"
        "  new(head); head^.v := 3; head^.next := nil;\n"
        "  other := head; writeln(other^.v)\n"
        "end.\n");
    EXPECT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(SemaTypeIdentity, InlineRecordParamExplainsWhyItDoesNotMatch) {
    // ISO §6.6.3.3 wants the same type, and two spellings of an anonymous
    // record are two types.  The point of the test is the explanation: the
    // message used to be "expected 'record (a)', got 'record (a)'".
    auto R = compileAndRun(
        "program p;\n"
        "var r: record a: integer end;\n"
        "procedure w(var q: record a: integer end);\n"
        "begin q.a := 5 end;\n"
        "begin w(r) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("two distinct types"), std::string::npos) << R.Stderr;
}

TEST(SemaDiagnostics, OperatorsUseSourceSpelling) {
    // Diagnostics used to print the TokenKind enumerator ("Plus").
    auto R = compileAndRun(
        "program p;\n"
        "var b: boolean; i: integer;\n"
        "begin b := true; i := 1; i := b + i end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("operator '+'"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("operator 'Plus'"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO runtime checks.
//
// Exit status 70 is what the runtime reporters use; see plang_sys.cpp.
// ---------------------------------------------------------------------------

TEST(RuntimeChecks, DivisionByZeroReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := 0; writeln('before'); writeln(a div b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    // Output preceding the fault must still be visible.
    EXPECT_EQ(R.Stdout, "before\n");
    EXPECT_NE(R.Stderr.find("div by zero"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, ModByZeroReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := 0; writeln(a mod b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("non-positive divisor 0"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, ModByNegativeReports) {
    // ISO §6.7.2.2 defines mod only for a positive divisor; a negative one has
    // no defined result, so accepting it would mean inventing one.
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 10; b := -3; writeln(a mod b) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("non-positive divisor -3"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, UnmatchedCaseReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := 99; case i of 1: writeln('one'); 2: writeln('two') end end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("case value 99 matches no label"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, MatchedCaseStillRuns) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := 2; case i of 1: writeln('one'); 2: writeln('two') end end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "two\n");
}

TEST(RuntimeChecks, ArrayIndexOutOfBoundsReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i;\n"
        "  i := 9; writeln(a[i])\n"
        "end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("array index 9 out of bounds 1..5"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, InBoundsIndexingUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..5] of integer; i: integer;\n"
        "begin\n"
        "  for i := 1 to 5 do a[i] := i * 10;\n"
        "  writeln(a[1] + a[5])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "60\n");
}

TEST(RuntimeChecks, SubrangeAssignmentReports) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 500; s := i; writeln(s) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("value 500 out of range 1..10"), std::string::npos)
        << R.Stderr;
}

TEST(RuntimeChecks, InRangeSubrangeAssignmentUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 7; s := i; writeln(s) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(RuntimeChecks, NoRangeChecksFlagOmitsThem) {
    // Division by zero stays checked; only the bounds checks are optional.
    auto R = compileAndRun(
        "program p;\n"
        "var s: 1..10; i: integer;\n"
        "begin i := 500; s := i; writeln('no check') end.\n",
        "-fno-range-checks");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "no check\n");
}

TEST(RuntimeChecks, DivisionByZeroCheckedEvenWithoutRangeChecks) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := 1; b := 0; writeln(a div b) end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("div by zero"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NilDerefReports) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

// The nil check had been grouped with the bounds checks, so asking for the
// bounds tests to be left out of a release build silently took this with it
// and a nil dereference became a signal with no line number.  Turning off the
// checks on indexing says nothing about wanting that.
TEST(RuntimeChecks, NilDerefCheckedEvenWithoutRangeChecks) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NoNilChecksFlagOmitsIt) {
    // Without the check the dereference reaches the hardware, so this is a
    // signal rather than a diagnostic — which is what the flag asks for.
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q^) end.\n",
        "-fno-nil-checks");
    EXPECT_NE(R.ExitCode, 70);
    EXPECT_EQ(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
}

TEST(RuntimeChecks, NoRangeChecksLeavesBoundsOutButKeepsNil) {
    // Both flags in one compilation: the bounds test is gone, the nil test
    // stays.  Written together because the two were one flag until 0.1.2.
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a: array[1..5] of integer; i: integer; q: pi;\n"
        "begin\n"
        "  i := 9; a[i] := 1; writeln('past the bound');\n"
        "  q := nil; writeln(q^)\n"
        "end.\n",
        "-fno-range-checks");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stdout.find("past the bound"), std::string::npos) << R.Stdout;
    EXPECT_NE(R.Stderr.find("dereference of nil"), std::string::npos) << R.Stderr;
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
// ISO §6.2.2.10 — a required identifier may be redeclared
//
// Codegen used to dispatch these on the spelling of the name alone, before
// anything had asked which declaration the name denoted here, so the required
// procedure or function won wherever the two were spelled alike: the program's
// own body was compiled and never called.
// ---------------------------------------------------------------------------

TEST(RedeclaredRequired, UserFunctionWins) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function abs(x: integer): integer;\n"
        "begin abs := 999 end;\n"
        "begin writeln(abs(-3)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "999\n");
}

TEST(RedeclaredRequired, RequiredFunctionStillReachedWhenNotRedeclared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(abs(-3)); writeln(round(2.6)); writeln(trunc(2.6)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n3\n2\n");
}

TEST(RedeclaredRequired, UserFunctionWinsInsideItsScopeOnly) {
    // The inner abs is the program's; the one called before it is declared, in
    // a procedure of its own, is still the required one.  Which declaration a
    // name denotes is a question about where the call is written, and dispatch
    // on spelling could not ask it.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure outer;\n"
        "begin writeln(abs(-7)) end;\n"
        "procedure inner;\n"
        "  function abs(x: integer): integer;\n"
        "  begin abs := 999 end;\n"
        "begin writeln(abs(-7)) end;\n"
        "begin outer; inner end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n999\n");
}

TEST(RedeclaredRequired, UserProcedureWins) {
    // A required procedure taking different arguments used to be reached with
    // none of them, which the IR verifier caught as a null operand.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure pack(n: integer);\n"
        "begin writeln('mine ', n:1) end;\n"
        "begin pack(7) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "mine 7\n");
}

// A routine the program declares used to be mangled `plang_<name>`, the
// namespace the runtime's own entry points live in, so `page` here collided
// with the runtime's `plang_page` and the link failed.  User code is mangled
// `pas_` now; see the mangling note in CodegenImpl.h.
TEST(RedeclaredRequired, UserProcedureWinsOverRuntimeSymbolName) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure page(n: integer);\n"
        "begin writeln('mine ', n:1) end;\n"
        "begin page(7) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "mine 7\n");
}

// Every runtime entry point a program is at all likely to name for itself, in
// one program, so that the mangling namespaces cannot quietly grow back into
// each other.  Each of these was a duplicate-symbol error before 0.1.3 in some
// program or other — which one depended on what else the program used, since
// the twin only reaches the link when its translation unit is pulled out of
// the archive for another reason.
TEST(RedeclaredRequired, UserRoutinesMayBeNamedAfterRuntimeSymbols) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure close;   begin write('a') end;\n"
        "procedure reset;   begin write('b') end;\n"
        "procedure rewrite; begin write('c') end;\n"
        "procedure halt;    begin write('d') end;\n"
        "procedure page;    begin write('e') end;\n"
        "function  round: integer;  begin round := 1 end;\n"
        "function  trunc: integer;  begin trunc := 2 end;\n"
        "function  sqrt: integer;   begin sqrt  := 3 end;\n"
        "function  sin: integer;    begin sin   := 4 end;\n"
        "function  ln: integer;     begin ln    := 5 end;\n"
        "begin\n"
        "  close; reset; rewrite; halt; page;\n"
        "  writeln(round:1, trunc:1, sqrt:1, sin:1, ln:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde12345\n");
}

// A global is mangled from its own prefix, and had the same problem.
TEST(RedeclaredRequired, UserGlobalsMayBeNamedAfterRuntimeSymbols) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var date, time, position, binding: integer;\n"
        "begin\n"
        "  date := 1; time := 2; position := 3; binding := 4;\n"
        "  writeln(date + time + position + binding)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

TEST(RedeclaredRequired, UserVariableNamedAfterRequiredFunction) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var odd: integer;\n"
        "begin odd := 42; writeln(odd) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// ---------------------------------------------------------------------------
// EP Tier 9 — Complex Number Type (§6.4.2.2 / §6.7.6.2–3 / §6.8.3.2)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// EP Tier 10 — Direct-Access File Handling (§6.4.3.6 / §6.7.5.2 / §6.7.6.5–6)
// Items 55–60: file[index] of T, extend, update, SeekRead/Write/Update,
//              position, LastPosition, empty
// ---------------------------------------------------------------------------

TEST(EP10DirectFile, DirectAccessFileTypeParsesAndCompiles) {
    // Item 55: file [index-type] of T should parse without errors.
    auto R = compileAndRun(
        "program p;\n"
        "type IntFile = file [1..10] of integer;\n"
        "var f: IntFile;\n"
        "    g: file [0..99] of integer;\n"
        "begin\n"
        "  writeln('ok')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ok\n");
}

TEST(EP10DirectFile, BinaryReadWrite) {
    // Binary typed-file I/O: write(f, v) and read(f, v) for file of integer.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 42;\n"
        "  write(f, v);\n"
        "  v := 99;\n"
        "  write(f, v);\n"
        "  update(f);\n"        // rewind to beginning (update on internal file)
        "  read(f, v);\n"
        "  writeln(v);\n"       // should print 42
        "  read(f, v);\n"
        "  writeln(v)\n"        // should print 99
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n99\n");
}

TEST(EP10DirectFile, SeekWriteAndSeekRead) {
    // Items 58: SeekWrite positions and writes; SeekRead positions and reads.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 10; seekwrite(f, 0); write(f, v);\n"
        "  v := 20; seekwrite(f, 1); write(f, v);\n"
        "  v := 30; seekwrite(f, 2); write(f, v);\n"
        "  seekread(f, 1);\n"   // seek to component 1
        "  read(f, v);\n"
        "  writeln(v);\n"       // 20
        "  seekread(f, 0);\n"   // seek to component 0
        "  read(f, v);\n"
        "  writeln(v);\n"       // 10
        "  seekread(f, 2);\n"   // seek to component 2
        "  read(f, v);\n"
        "  writeln(v)\n"        // 30
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "20\n10\n30\n");
}

TEST(EP10DirectFile, SeekUpdateReadAndWrite) {
    // Item 58: SeekUpdate — read then write at same position.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 100; seekwrite(f, 0); write(f, v);\n"
        "  v := 200; seekwrite(f, 1); write(f, v);\n"
        "  seekupdate(f, 1);\n"    // position at component 1 for update
        "  read(f, v);\n"          // read 200
        "  v := v + 5;\n"
        "  seekwrite(f, 1);\n"     // overwrite component 1
        "  write(f, v);\n"
        "  seekread(f, 1);\n"
        "  read(f, v);\n"
        "  writeln(v)\n"            // 205
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "205\n");
}

TEST(EP10DirectFile, PositionFunction) {
    // Item 59: position(f) returns the current component index.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v, p: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 1; write(f, v);\n"
        "  v := 2; write(f, v);\n"
        "  v := 3; write(f, v);\n"
        "  p := position(f);\n"   // position after writing 3 items = 3
        "  writeln(p);\n"
        "  seekwrite(f, 0);\n"
        "  p := position(f);\n"   // back at 0
        "  writeln(p);\n"
        "  seekwrite(f, 2);\n"
        "  p := position(f);\n"   // at 2
        "  writeln(p)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n0\n2\n");
}

TEST(EP10DirectFile, LastPositionFunction) {
    // Item 59: lastposition(f) returns the index of the last component.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v, lp: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 10; write(f, v);\n"
        "  v := 20; write(f, v);\n"
        "  v := 30; write(f, v);\n"
        "  lp := lastposition(f);\n"  // 3 components → lastpos = 2
        "  writeln(lp)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(EP10DirectFile, EmptyOnNewFile) {
    // Item 60: empty(f) is true for a freshly rewritten (empty) file.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  if empty(f) then writeln('empty') else writeln('not-empty')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "empty\n");
}

TEST(EP10DirectFile, EmptyAfterSeekPastEnd) {
    // Item 60: empty(f) is false when position <= lastposition,
    //          true when position > lastposition (past the last component).
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 42; write(f, v);\n"
        "  { file now has 1 component at index 0; position = 1 (past end) }\n"
        "  if empty(f) then writeln('past-end') else writeln('not-past-end');\n"
        "  seekread(f, 0);\n"   // seek back to component 0
        "  if empty(f) then writeln('past-end') else writeln('at-component')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "past-end\nat-component\n");
}

TEST(EP10DirectFile, ExtendAppendsContent) {
    // Item 56: extend(f) opens for appending; existing content preserved.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 11; write(f, v);\n"  // component 0
        "  v := 22; write(f, v);\n"  // component 1
        "  extend(f);\n"              // seek to end; preserve content
        "  v := 33; write(f, v);\n"  // component 2
        "  update(f);\n"              // rewind to beginning
        "  read(f, v); writeln(v);\n" // 11
        "  read(f, v); writeln(v);\n" // 22
        "  read(f, v); writeln(v)\n"  // 33
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n22\n33\n");
}

TEST(EP10DirectFile, UpdateRewrites) {
    // Item 57: update(f) opens for read+write without truncating.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  v := 7; write(f, v);\n"   // write 7 at component 0
        "  update(f);\n"              // rewind
        "  read(f, v);\n"             // read back 7
        "  writeln(v);\n"
        "  update(f);\n"              // rewind again
        "  v := 99; write(f, v);\n"  // overwrite component 0 with 99
        "  update(f);\n"              // rewind
        "  read(f, v);\n"             // read 99
        "  writeln(v)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n99\n");
}

// ---------------------------------------------------------------------------
// ISO §6.5.5 / §6.9.1: the buffer variable
//
// f^ is the component at the file's current position: reset and get fill it,
// put appends it, and read and write are defined in terms of it.  A file of
// anything but a scalar can only be reached this way, so these also stand in
// for typed-file I/O generally.
// ---------------------------------------------------------------------------

TEST(BufferVariable, PutWritesWhatWasAssignedToIt) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 4 do begin f^ := i * i; put(f) end;\n"
        "  reset(f);\n"
        "  while not eof(f) do begin write(f^, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 4 9 16 \n");
}

TEST(BufferVariable, ResetLeavesTheFirstComponentInIt) {
    // §6.5.5: after reset, f^ is the first component, and read(f,v) is
    // v := f^; get(f) — so the two views agree before and after.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "    i, v: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 3 do begin f^ := i; put(f) end;\n"
        "  reset(f);\n"
        "  v := f^;\n"
        "  read(f, i);\n"
        "  writeln(v, ' ', i, ' ', f^)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 1 2\n");
}

TEST(BufferVariable, CarriesARecordComponent) {
    // A file of records has no textual form, so the buffer variable is the
    // only way to reach one.
    auto R = compileAndRun(
        "program p;\n"
        "type rec = record a: integer; b: char end;\n"
        "var f: file of rec;\n"
        "    r: rec;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  r.a := 7; r.b := 'x'; f^ := r; put(f);\n"
        "  r.a := 8; r.b := 'y'; f^ := r; put(f);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin write(f^.a, f^.b, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7x 8y \n");
}

TEST(BufferVariable, WriteAndReadCarryARecordToo) {
    // §6.9.1: on a file that is not a textfile, write(f,e) is f^ := e; put(f),
    // so a record is a write-parameter like any other component.
    auto R = compileAndRun(
        "program p;\n"
        "type rec = record a: integer; b: char end;\n"
        "var f: file of rec;\n"
        "    r: rec;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  r.a := 1; r.b := 'p'; write(f, r);\n"
        "  r.a := 2; r.b := 'q'; write(f, r);\n"
        "  reset(f);\n"
        "  read(f, r); write(r.a, r.b, ' ');\n"
        "  read(f, r); writeln(r.a, r.b)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1p 2q\n");
}

TEST(BufferVariable, CarriesAnArrayComponent) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "var f: file of row;\n"
        "    r: row;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 3 do r[i] := i * 5;\n"
        "  write(f, r);\n"
        "  reset(f);\n"
        "  read(f, r);\n"
        "  for i := 1 to 3 do write(r[i], ' ');\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 10 15 \n");
}

TEST(BufferVariable, ReadsAndWritesATextFileOneCharacterAtATime) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text;\n"
        "    c: char;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  f^ := 'a'; put(f);\n"
        "  f^ := 'b'; put(f);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin c := f^; write(c); get(f) end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Trailing space: the two puts left a line with no marker, and §6.4.3.5
    // makes a text file a sequence of terminated lines, so reading it back
    // finds the marker that closes the one that was written.
    EXPECT_EQ(R.Stdout, "ab \n");
}

TEST(BufferVariable, IsTheComponentAtThePositionSeekReadChose) {
    // The buffer is filled by peeking, so position(f) still reports where f^
    // itself is rather than the component after it.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file[1..10] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 5 do write(f, i * 2);\n"
        "  seekread(f, 2);\n"
        "  writeln(position(f), ' ', f^, ' ', lastposition(f));\n"
        "  get(f);\n"
        "  writeln(position(f), ' ', f^)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 6 4\n3 8\n");
}

TEST(BufferVariable, PutAfterSeekWriteOverwritesThatComponent) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file[1..10] of integer;\n"
        "    i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 1 to 4 do write(f, i);\n"
        "  seekwrite(f, 1); f^ := 99; put(f);\n"
        "  seekread(f, 0);\n"
        "  for i := 1 to 4 do begin write(f^, ' '); get(f) end;\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 99 3 4 \n");
}

TEST(BufferVariable, AWrittenValueBecomesAComponentOfTheFilesType) {
    // §6.9.1: write(f,e) is f^ := e, so an integer written to a file of real
    // widens on the way in rather than being read back as a real.
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of real;\n"
        "    r: real;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  write(f, 3);\n"
        "  write(f, 2.5);\n"
        "  reset(f);\n"
        "  read(f, r); write(r:0:1, ' ');\n"
        "  read(f, r); writeln(r:0:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0 2.5\n");
}

TEST(BufferVariable, AValueThatIsNoComponentIsTurnedAway) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: file of integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  write(f, 1.5)\n"
        "end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("type mismatch in write"), std::string::npos) << R.Stderr;
}

TEST(EP9Complex, CmplxConstructor) {
    // cmplx(re, im) constructor; re() and im() extractors.
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n4.0\n");
}

TEST(EP9Complex, PolarConstructor) {
    // polar(r, 0) = (r, 0i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := polar(2.0, 0.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2.0\n0.0\n");
}

TEST(EP9Complex, Addition) {
    // (1+2i) + (3+4i) = (4+6i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) + cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4.0\n6.0\n");
}

TEST(EP9Complex, Subtraction) {
    // (5+7i) - (2+3i) = (3+4i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(5.0, 7.0) - cmplx(2.0, 3.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n4.0\n");
}

TEST(EP9Complex, Multiplication) {
    // (1+2i)*(3+4i) = (3-8) + (4+6)i = -5+10i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) * cmplx(3.0, 4.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):2:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-5.0\n10.0\n");
}

TEST(EP9Complex, Division) {
    // (1+2i) / (1+0i) = (1+2i)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 2.0) / cmplx(1.0, 0.0);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1.0\n2.0\n");
}

TEST(EP9Complex, AbsComplex) {
    // |3+4i| = 5.0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(3.0, 4.0);\n"
        "  writeln(abs(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0\n");
}

TEST(EP9Complex, ReIm) {
    // re and im extraction
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(2.5, 3.5);\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2.5\n3.5\n");
}

TEST(EP9Complex, Arg) {
    // arg(0+1i) = pi/2 ≈ 1.5708; write rounded to 4 decimals
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    a: real;\n"
        "begin\n"
        "  c := cmplx(0.0, 1.0);\n"
        "  a := arg(c);\n"
        "  writeln(a:1:4)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1.5708\n");
}

TEST(EP9Complex, SqrtComplex) {
    // sqrt(-1+0i) ≈ 0+1i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    r: complex;\n"
        "begin\n"
        "  c := cmplx(-1.0, 0.0);\n"
        "  r := sqrt(c);\n"
        "  writeln(re(r):1:1);\n"
        "  writeln(im(r):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0.0\n1.0\n");
}

TEST(EP9Complex, SinComplex) {
    // sin(0+0i) = 0+0i
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "    r: complex;\n"
        "begin\n"
        "  c := cmplx(0.0, 0.0);\n"
        "  r := sin(c);\n"
        "  writeln(re(r):1:1);\n"
        "  writeln(im(r):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0.0\n0.0\n");
}

TEST(EP9Complex, Widening) {
    // real -> complex widening: c := 3.14 sets re=3.14, im=0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := 3.14;\n"
        "  writeln(re(c):1:2);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.14\n0.0\n");
}

TEST(EP9Complex, IntegerWidening) {
    // integer -> complex widening: c := 42 sets re=42, im=0
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := 42;\n"
        "  writeln(re(c):2:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42.0\n0.0\n");
}

TEST(EP9Complex, MixedArith) {
    // cmplx(1,0) + 2.0 (complex + real) = cmplx(3,0)
    auto R = compileAndRun(
        "program p;\n"
        "var c: complex;\n"
        "begin\n"
        "  c := cmplx(1.0, 0.0) + 2.0;\n"
        "  writeln(re(c):1:1);\n"
        "  writeln(im(c):1:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0\n0.0\n");
}

// ---------------------------------------------------------------------------
// EP Tier 5 — Function/Procedure Enhancements
// ---------------------------------------------------------------------------

// §6.7.2: named result variable
TEST(EP5NamedResult, BasicAssignAndReturn) {
    auto R = compileAndRun(
        "program p;\n"
        "function double(n: integer) = result : integer;\n"
        "begin result := n * 2 end;\n"
        "begin writeln(double(7)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "14\n");
}

TEST(EP5NamedResult, FunctionNameStillWorks) {
    // Traditional assignment to the function name must still work even when
    // a result variable is also declared.
    auto R = compileAndRun(
        "program p;\n"
        "function triple(n: integer) = res : integer;\n"
        "begin triple := n * 3 end;\n"
        "begin writeln(triple(5)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "15\n");
}

TEST(EP5NamedResult, ReadResultVariable) {
    // The result variable can be both read and written inside the body.
    auto R = compileAndRun(
        "program p;\n"
        "function fib(n: integer) = r : integer;\n"
        "begin\n"
        "  if n <= 1 then r := n\n"
        "  else r := fib(n-1) + fib(n-2)\n"
        "end;\n"
        "begin writeln(fib(8)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "21\n");
}

// §6.7.3.1: protected parameters
TEST(EP5Protected, AssignmentToProtectedRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q(protected x: integer);\n"
        "begin x := 5 end;\n"
        "begin q(1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("protected") != std::string::npos) << R.Stderr;
}

TEST(EP5Protected, ReadingProtectedParamAllowed) {
    auto R = compileAndRun(
        "program p;\n"
        "function double(protected n: integer): integer;\n"
        "begin double := n * 2 end;\n"
        "begin writeln(double(6)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "12\n");
}

// §6.4.9: type of x
TEST(EP5TypeOf, BasicTypeInquiry) {
    // var y: type of x; should give y the same type as x (integer here).
    auto R = compileAndRun(
        "program p;\n"
        "var x: integer;\n"
        "var y: type of x;\n"
        "begin x := 42; y := x; writeln(y) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(EP5TypeOf, TypeOfInParamList) {
    // EP §6.4.9: type of x as a parameter type; x is integer so the param
    // is integer too. (Using integer avoids float-formatting ambiguity.)
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure show(x: type of v);\n"
        "begin writeln(x) end;\n"
        "begin show(99) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// §6.9.3.9.3: for v in set-expr do
TEST(EP5ForIn, IteratesSetMembers) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 1..5;\n"
        "var v, total: integer;\n"
        "begin\n"
        "  s := [1, 3, 5];\n"
        "  total := 0;\n"
        "  for v in s do total := total + v;\n"
        "  writeln(total)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9\n");
}

TEST(EP5ForIn, EmptySetIteratesZeroTimes) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: set of 0..10;\n"
        "var v, count: integer;\n"
        "begin\n"
        "  s := [];\n"
        "  count := 0;\n"
        "  for v in s do count := count + 1;\n"
        "  writeln(count)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n");
}

TEST(EP5ForIn, NonSetExprRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var x, v: integer;\n"
        "begin for v in x do writeln(v) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("set") != std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.7.3.7: Conformant Array Parameters (Tier 6, features #35–#38)
// ---------------------------------------------------------------------------

// #35: Value conformant array param — sum elements using lo..hi bounds.
TEST(EP6ConformantArray, ValueParamSum) {
    auto R = compileAndRun(
        "program p;\n"
        "function sumArr(A: array [lo..hi : integer] of integer) : integer;\n"
        "var i, s: integer;\n"
        "begin\n"
        "  s := 0;\n"
        "  for i := lo to hi do s := s + A[i];\n"
        "  sumArr := s\n"
        "end;\n"
        "var arr: array [1..5] of integer;\n"
        "begin\n"
        "  arr[1] := 10; arr[2] := 20; arr[3] := 30;\n"
        "  arr[4] := 40; arr[5] := 50;\n"
        "  writeln(sumArr(arr))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "150\n");
}

// #35: Value conformant array — use lo and hi bound variables in body.
TEST(EP6ConformantArray, ValueParamUseBounds) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure showbounds(A: array [lo..hi : integer] of integer);\n"
        "begin\n"
        "  writeln(lo);\n"
        "  writeln(hi)\n"
        "end;\n"
        "var arr: array [3..7] of integer;\n"
        "begin\n"
        "  showbounds(arr)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n");
}

// #36: Variable conformant array param — fill array via var param.
TEST(EP6ConformantArray, VarParamFill) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure fill(var A: array [lo..hi : integer] of integer; v: integer);\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := lo to hi do A[i] := v\n"
        "end;\n"
        "var arr: array [1..4] of integer;\n"
        "var i: integer;\n"
        "begin\n"
        "  fill(arr, 7);\n"
        "  for i := 1 to 4 do writeln(arr[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n7\n7\n");
}

// #35: Call same conformant procedure with two different-sized arrays.
TEST(EP6ConformantArray, DifferentSizedArrays) {
    auto R = compileAndRun(
        "program p;\n"
        "function countElems(A: array [lo..hi : integer] of integer) : integer;\n"
        "begin\n"
        "  countElems := hi - lo + 1\n"
        "end;\n"
        "var a3: array [1..3] of integer;\n"
        "var a7: array [1..7] of integer;\n"
        "begin\n"
        "  writeln(countElems(a3));\n"
        "  writeln(countElems(a7))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n");
}

// #35: Value conformant with 0-based array.
TEST(EP6ConformantArray, ZeroBasedArray) {
    auto R = compileAndRun(
        "program p;\n"
        "function first(A: array [lo..hi : integer] of integer) : integer;\n"
        "begin first := A[lo] end;\n"
        "var arr: array [0..2] of integer;\n"
        "begin\n"
        "  arr[0] := 99;\n"
        "  writeln(first(arr))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// #36: Conformant var param — modify and read back.
TEST(EP6ConformantArray, VarParamModifyReadback) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure setFirst(var A: array [lo..hi : integer] of integer; v: integer);\n"
        "begin A[lo] := v end;\n"
        "var arr: array [2..5] of integer;\n"
        "begin\n"
        "  arr[2] := 0;\n"
        "  setFirst(arr, 42);\n"
        "  writeln(arr[2])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// #37: Multi-dimensional abbreviated form — abbreviated syntax
// `array [lo..hi : T1; lo2..hi2 : T2] of E` parses correctly and expands to
// nested conformant schemas. This test verifies the abbreviated form compiles
// and that all bound variables are visible in the procedure body.
TEST(EP6ConformantArray, MultiDimAbbreviatedSyntax) {
    // Declare a 2D conformant param using the abbreviated form.
    // The abbreviated syntax expands at parse time to nested schemas.
    auto R = compileAndRun(
        "program p;\n"
        "{ abbreviated 2D conformant syntax — reads only bound vars }\n"
        "procedure showBounds(A: array [lo..hi : integer; c1..c2 : integer] of integer);\n"
        "begin\n"
        "  writeln(lo); writeln(hi); writeln(c1); writeln(c2)\n"
        "end;\n"
        "{ type-compatible actual: array of array for the 2D conformant }\n"
        "type Row = array [10..12] of integer;\n"
        "var mat: array [3..5] of Row;\n"
        "begin\n"
        "  showBounds(mat)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n5\n10\n12\n");
}

TEST(EP6ConformantArray, MultiDimElementAccess) {
    // The array arrives as one flat block, so an element of it can only be
    // found by folding the subscripts against the runtime bounds of every
    // dimension.
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[1..2, 1..3] of integer;\n"
        "var a: m;\n"
        "    i, j: integer;\n"
        "function total(var x: array[l1..h1: integer;\n"
        "                            l2..h2: integer] of integer): integer;\n"
        "var r, s, t: integer;\n"
        "begin\n"
        "  t := 0;\n"
        "  for r := l1 to h1 do\n"
        "    for s := l2 to h2 do\n"
        "      t := t + x[r, s];\n"
        "  total := t\n"
        "end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;\n"
        "  writeln(total(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "102\n");   // 11+12+13 + 21+22+23
}

TEST(EP6ConformantArray, MultiDimAssignmentThroughAVarParameter) {
    // Chained subscripts reach the same element as the comma form, and the
    // dimensions need not start at 1.
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[0..1, 5..7] of integer;\n"
        "var a: m;\n"
        "    i, j: integer;\n"
        "procedure fill(var x: array[l1..h1: integer;\n"
        "                            l2..h2: integer] of integer);\n"
        "var r, s: integer;\n"
        "begin\n"
        "  for r := l1 to h1 do\n"
        "    for s := l2 to h2 do\n"
        "      x[r][s] := r * 100 + s\n"
        "end;\n"
        "begin\n"
        "  fill(a);\n"
        "  for i := 0 to 1 do\n"
        "    for j := 5 to 7 do write(a[i, j], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5 6 7 105 106 107 \n");
}

TEST(EP6ConformantArray, MultiDimRowWidthComesFromTheActual) {
    // Two actuals of different widths reach the same routine; the row stride
    // has to come from the bounds passed with each of them.
    auto R = compileAndRun(
        "program p;\n"
        "type small = array[1..2, 1..2] of integer;\n"
        "     big   = array[1..2, 1..4] of integer;\n"
        "var s: small;\n"
        "    b: big;\n"
        "    i, j: integer;\n"
        "function corner(var x: array[l1..h1: integer;\n"
        "                             l2..h2: integer] of integer): integer;\n"
        "begin corner := x[h1, h2] end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 2 do s[i, j] := i * j;\n"
        "  for i := 1 to 2 do for j := 1 to 4 do b[i, j] := i * j;\n"
        "  writeln(corner(s), ' ', corner(b))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 8\n");
}

TEST(EP6ConformantArray, ThreeDimensions) {
    auto R = compileAndRun(
        "program p;\n"
        "type m = array[1..2, 1..2, 1..2] of integer;\n"
        "var a: m;\n"
        "    i, j, k: integer;\n"
        "function total(var x: array[a1..b1: integer;\n"
        "                            a2..b2: integer;\n"
        "                            a3..b3: integer] of integer): integer;\n"
        "var r, s, t, n: integer;\n"
        "begin\n"
        "  n := 0;\n"
        "  for r := a1 to b1 do\n"
        "    for s := a2 to b2 do\n"
        "      for t := a3 to b3 do n := n + x[r, s, t];\n"
        "  total := n\n"
        "end;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 2 do for k := 1 to 2 do\n"
        "    a[i, j, k] := i * 100 + j * 10 + k;\n"
        "  writeln(total(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1332\n");  // each of i, j, k is 1 four times and 2 four times
}

TEST(MultiDimIndexing, ReadsAnElementUnderExtendedPascalToo) {
    // EP §6.8.7 spells a structured value TypeName[...], which reads like a
    // subscript list; a variable's name is not a type name, so a[i, j] still
    // indexes.
    auto R = compileAndRun(
        "program p;\n"
        "type digits = set of 0..9;\n"
        "var a: array[1..2, 1..2] of integer;\n"
        "    s: digits;\n"
        "begin\n"
        "  a[1, 1] := 4;\n"
        "  a[2, 2] := 7;\n"
        "  s := digits[1, 3, 5];\n"
        "  writeln(a[1, 1] + a[2, 2], ' ', card(s))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 3\n");
}

// #38: Protected conformant array — cannot assign to elements.
TEST(EP6ConformantArray, ProtectedConformantRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure tryWrite(protected A: array [lo..hi : integer] of integer);\n"
        "begin A[lo] := 99 end;\n"
        "var arr: array [1..3] of integer;\n"
        "begin tryWrite(arr) end.\n", kEP);
    // Should fail to compile because 'protected' param cannot be assigned.
    EXPECT_NE(R.ExitCode, 0);
}

// #35/#36: Conformant array param type checking — wrong element type rejected.
TEST(EP6ConformantArray, ElemTypeMismatchRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure proc(A: array [lo..hi : integer] of integer);\n"
        "begin end;\n"
        "var arr: array [1..3] of real;\n"
        "begin proc(arr) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant") != std::string::npos ||
                R.Stderr.find("mismatch")   != std::string::npos) << R.Stderr;
}

// #35: Conformant param passed a non-array argument is rejected.
TEST(EP6ConformantArray, NonArrayActualRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure proc(A: array [lo..hi : integer] of integer);\n"
        "begin end;\n"
        "var x: integer;\n"
        "begin proc(x) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant") != std::string::npos ||
                R.Stderr.find("array")      != std::string::npos) << R.Stderr;
}

// #36: Pass conformant actual to another conformant param (forward bounds).
TEST(EP6ConformantArray, ConformantPassThrough) {
    auto R = compileAndRun(
        "program p;\n"
        "function sumArr(A: array [lo..hi : integer] of integer) : integer;\n"
        "var i, s: integer;\n"
        "begin\n"
        "  s := 0;\n"
        "  for i := lo to hi do s := s + A[i];\n"
        "  sumArr := s\n"
        "end;\n"
        "procedure wrapper(var B: array [lo2..hi2 : integer] of integer);\n"
        "begin writeln(sumArr(B)) end;\n"
        "var arr: array [1..4] of integer;\n"
        "begin\n"
        "  arr[1] := 1; arr[2] := 2; arr[3] := 3; arr[4] := 4;\n"
        "  wrapper(arr)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

// ISO 7185 level 1: conformant array parameters are standard Pascal, not an
// Extended Pascal extension, and were rejected under -std=iso7185 for as long
// as they had existed.
TEST(ISO7185Level1, ConformantArrayIsStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array [1..4] of integer; i: integer;\n"
        "function total(x: array [lo..hi: integer] of integer): integer;\n"
        "var j, s: integer;\n"
        "begin s := 0; for j := lo to hi do s := s + x[j]; total := s end;\n"
        "begin for i := 1 to 4 do a[i] := i; writeln(total(a)) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n");
}

// ISO 7185 §6.6.3.7.1: the packed form of the schema, which is how a string of
// any length is passed.  It did not parse at all: in a parameter list `packed`
// went to the ordinary array parser, which stopped at the ':' in the bounds.
TEST(ISO7185Level1, PackedConformantArray) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: packed array [1..5] of char;\n"
        "    b: packed array [1..11] of char;\n"
        "procedure show(s: packed array [lo..hi: integer] of char);\n"
        "var i: integer;\n"
        "begin for i := lo to hi do write(s[i]); writeln end;\n"
        "begin a := 'hello'; b := 'hello world'; show(a); show(b) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello\nhello world\n");
}

// ISO 7185 §6.6.3.7.1: the packed form takes one index-type-specification;
// only the unpacked form may name several.
TEST(ISO7185Level1, PackedConformantArrayIsOneDimension) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q(s: packed array [lo..hi: integer;\n"
        "                             j..k: integer] of char);\n"
        "begin end;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("one dimension") != std::string::npos) << R.Stderr;
}

// A conformant array parameter is a type of its own, so no array is assignable
// to it whole.  This was allowed, and copied the source array's length into
// whatever the caller had passed: eight elements written through a parameter
// bound to an array of three ran five past the end of it.
TEST(ISO7185Level1, WholeArrayAssignmentToConformantRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var big: array [1..8] of integer; small: array [1..3] of integer;\n"
        "procedure q(var x: array [lo..hi: integer] of integer);\n"
        "begin x := big end;\n"
        "begin q(small) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("conformant array") != std::string::npos) << R.Stderr;
}

// ISO 7185 §6.6.5.4: pack and unpack take any array, and a conformant array
// parameter is one — its bounds are values rather than numbers, which is a
// matter for the code generator.  Sema let it through and the generator, which
// wanted two arrays with bounds it could read off their types, stopped the
// compiler with an internal error.
TEST(ISO7185Level1, PackAndUnpackTakeAConformantArray) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var u: array [1..5] of char; z: packed array [1..5] of char;\n"
        "    wide: array [1..9] of char; i: integer;\n"
        "procedure topacked(var x: array [lo..hi: integer] of char);\n"
        "begin pack(x, lo, z) end;\n"
        "procedure fromacked(var x: array [lo..hi: integer] of char);\n"
        "begin unpack(z, x, lo) end;\n"
        "begin\n"
        "  u[1] := 'a'; u[2] := 'b'; u[3] := 'c'; u[4] := 'd'; u[5] := 'e';\n"
        "  topacked(u); writeln(z);\n"
        "  for i := 1 to 9 do wide[i] := '.';\n"
        "  fromacked(wide);\n"
        "  for i := 1 to 9 do write(wide[i]); writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde\nabcde....\n");
}

// The operands of pack and unpack were never checked to be arrays, so one that
// was not reached a generator with nothing to lower.
TEST(ISO7185Level1, PackOnANonArrayIsDiagnosed) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer; z: packed array [1..3] of char;\n"
        "begin pack(i, 1, z) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("expects an array") != std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// The Pascal Acceptance Test (test/Acceptance) is a whole standard program,
// and each of these is one thing it found.  They are kept apart from it
// because a 3000-line program that stops at the first of eight errors says
// only that something is wrong.
// ---------------------------------------------------------------------------

// ISO §6.8.3.9 forbids a threat to "the variable denoted by the
// control-variable".  The scan compared the *spelling* of every assignment
// target in every procedure of the block against the loop's variable, so a
// procedure with a local of its own name was reported as assigning to a
// variable it cannot see.  `i` being the usual name for both, this rejected
// most programs of any size: 254 of the acceptance test's 292 errors.
TEST(ForThreat, ALocalOfTheSameNameIsADifferentVariable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure helper;\n"
        "var i: integer;\n"
        "begin i := 99 end;\n"
        "begin\n"
        "  for i := 1 to 3 do helper;\n"
        "  writeln('done')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "done\n");
}

TEST(ForThreat, AProcedureWithNoLocalStillThreatens) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure threat;\n"
        "begin i := 99 end;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("control variable 'i'") != std::string::npos)
        << R.Stderr;
}

// The declaration part contains the procedures nested inside it too.
TEST(ForThreat, ANestedProcedureThreatensAsWell) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "procedure outer;\n"
        "procedure nested;\n"
        "begin i := 99 end;\n"
        "begin nested end;\n"
        "begin for i := 1 to 3 do writeln(i) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("control variable 'i'") != std::string::npos)
        << R.Stderr;
}

// ISO §6.1.6: labels "shall be distinguished by their apparent integral
// values", and "the spelling of a label shall be its apparent integral value".
TEST(Labels, LeadingZeroesNameTheSameLabel) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 003;\n"
        "begin\n"
        "  goto 3;\n"
        "  writeln('skipped');\n"
        "  03: writeln('reached')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "reached\n");
}

TEST(Labels, OutsideZeroToNineThousandNineHundredNinetyNine) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 10000;\n"
        "begin 10000: writeln('x') end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("outside the range 0 to 9999") != std::string::npos)
        << R.Stderr;
}

// A subrange takes its values from its host type (ISO §6.4.2.4), so a subrange
// of integer holds integers.  '+', '-' and '*' accepted them and div and mod
// did not.
TEST(Subranges, DivAndModTakeThem) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a, b: 0..100; c: -100..100;\n"
        "begin\n"
        "  a := 78; b := 43; c := -50;\n"
        "  writeln(a div b:1, ' ', a mod b:1, ' ', c div 7:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 35 -7\n");
}

TEST(Subranges, DivStillWantsAnInteger) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var r: real;\n"
        "begin r := 1.0; writeln(r div 2) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("requires integer operands") != std::string::npos)
        << R.Stderr;
}

// ISO §6.4.5 b) makes two subranges of one host type compatible whatever
// bounds each was written with; §6.4.6 c) leaves whether the value fits to be
// reported when the assignment happens.  Requiring the bounds to agree made
// this a type error.
TEST(Subranges, OneIsAssignableToAnotherOfDifferentBounds) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: 1..10; b: 1..100;\n"
        "begin b := 5; a := b; writeln(a:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

TEST(Subranges, AValueOutsideTheDestinationIsStillReported) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: 1..10; b: 1..100;\n"
        "begin b := 50; a := b; writeln(a:1) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("out of range") != std::string::npos) << R.Stderr;
}

// Relaxing the bounds must not reach arrays: two array types of different
// extents are not one type, and assigning between them would write past the
// shorter.
TEST(Subranges, ArraysOfDifferentExtentsAreStillDistinct) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..10] of integer; b: array[1..5] of integer;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("cannot assign") != std::string::npos) << R.Stderr;
}

// ISO §6.4.2.2: maxint is a constant, so it may stand wherever one may.
// Codegen knew its value and Sema did not, so a bound written with it was
// rejected as though the name were a variable.
TEST(Constants, MaxintIsAConstantExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var v: -maxint..maxint;\n"
        "begin v := -5; writeln(v:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-5\n");
}

// ISO §6.1.9: '@' is the alternative for '^' and the two are not to be
// distinguished, so a pointer type and a dereference may be written either way
// — including both ways for the one variable.
TEST(LexicalAlternatives, AtSignDenotesAPointer) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type iptr = @integer;\n"
        "var q: iptr; r: ^integer;\n"
        "begin\n"
        "  new(q); q@ := 42;\n"
        "  new(r); r^ := 7;\n"
        "  writeln(q@:1, ' ', q^:1, ' ', r@:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42 42 7\n");
}

// ISO §6.8.2.2 asks that "the function-block associated with the
// function-identifier of an assignment-statement shall contain the
// assignment-statement" — contain, not be.  A function declared inside another
// is contained in it, so it may assign the outer one's result.
TEST(FunctionResult, ANestedFunctionMayAssignTheEnclosingResult) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var n: integer;\n"
        "function outer: integer;\n"
        "var i: integer;\n"
        "  function inner: integer;\n"
        "  begin\n"
        "    inner := 12;\n"
        "    outer := 37\n"
        "  end;\n"
        "begin i := inner; writeln('inner ', i:1) end;\n"
        "begin n := outer; writeln('outer ', n:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "inner 12\nouter 37\n");
}

// The assignment written in the nested function is the one that satisfies the
// outer function, so it must not still be reported as never assigning.
TEST(FunctionResult, TheNestedAssignmentSatisfiesTheOuterFunction) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function outer: integer;\n"
        "var i: integer;\n"
        "  function inner: integer;\n"
        "  begin inner := 1; outer := 5 end;\n"
        "begin i := inner end;\n"
        "begin writeln(outer:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// A function that assigns nothing anywhere is still reported.
TEST(FunctionResult, OneThatNeverAssignsIsStillReported) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function f: integer;\n"
        "begin end;\n"
        "begin writeln(f) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("does not assign to its result") != std::string::npos)
        << R.Stderr;
}

// A local of the function's name denotes the local, the nearer declaration
// winning, so assigning it is not assigning the result.
TEST(FunctionResult, ALocalOfTheSameNameIsNotTheResult) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var n: integer;\n"
        "function f: integer;\n"
        "  procedure g;\n"
        "  var f: integer;\n"
        "  begin f := 1; writeln('local ', f:1) end;\n"
        "begin g; f := 2 end;\n"
        "begin n := f; writeln('result ', n:1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "local 1\nresult 2\n");
}

TEST(Constants, MaxcharIsOneToo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: 'a'..maxchar;\n"
        "begin c := 'q'; writeln(c) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "q\n");
}

// ---------------------------------------------------------------------------
// EP Tier 7 — Schema System (EP §6.4.7–§6.8.4)
// ---------------------------------------------------------------------------

// #39: Basic schema definition and array element use.
TEST(EP7Schema, BasicDefinitionAndUse) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of real;\n"
        "var v: Vector(5);\n"
        "begin\n"
        "  v[1] := 1.0; v[2] := 2.0; v[3] := 3.0;\n"
        "  writeln(v[2]:1:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

// #41: Discriminant access v.n returns the discriminant value.
TEST(EP7Schema, DiscriminantAccess) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of integer;\n"
        "var v: Vector(5);\n"
        "begin\n"
        "  writeln(v.n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// #42: 'with' on a schema instance exposes discriminant identifiers.
TEST(EP7Schema, WithExposesDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "var v: Vec(7);\n"
        "begin\n"
        "  with v do writeln(n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

// Two schema instances of the same schema with different discriminants are independent.
TEST(EP7Schema, TwoDifferentInstances) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vector(n: integer) = array[1..n] of integer;\n"
        "var a: Vector(3);\n"
        "var b: Vector(7);\n"
        "begin\n"
        "  a[1] := 10; a[2] := 20; a[3] := 30;\n"
        "  b[1] := 100;\n"
        "  writeln(a.n);\n"
        "  writeln(b.n);\n"
        "  writeln(a[2]);\n"
        "  writeln(b[1])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n7\n20\n100\n");
}

// #44: Schema as value parameter type.
TEST(EP7Schema, SchemaAsValueParam) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "function first(v: Vec(3)) : integer;\n"
        "begin first := v[1] end;\n"
        "var a: Vec(3);\n"
        "begin\n"
        "  a[1] := 42;\n"
        "  writeln(first(a))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// #43: new(p) for a pointer to a schema instance.
TEST(EP7Schema, NewForSchemaPointer) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "type VecPtr = ^Vec(4);\n"
        "var ptr: VecPtr;\n"
        "begin\n"
        "  new(ptr);\n"
        "  ptr^[1] := 99;\n"
        "  writeln(ptr^[1])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n");
}

// Wrong argument count for schema instantiation is an error.
TEST(EP7Schema, WrongArgCountRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "type Vec(n: integer) = array[1..n] of integer;\n"
        "var v: Vec(1, 2);\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_TRUE(R.Stderr.find("discriminant") != std::string::npos ||
                R.Stderr.find("expects")      != std::string::npos ||
                R.Stderr.find("schema")       != std::string::npos) << R.Stderr;
}

// #45: 'bindable' prefix on a type definition is accepted without error.
TEST(EP7Schema, BindableKeyword) {
    auto R = compileAndRun(
        "program p;\n"
        "type T = bindable integer;\n"
        "var x: T;\n"
        "begin x := 5; writeln(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5\n");
}

// Schema whose body is a record (multi-field schema).
TEST(EP7Schema, RecordSchema) {
    auto R = compileAndRun(
        "program p;\n"
        "type Pair(n: integer) = record\n"
        "  x: array[1..n] of integer;\n"
        "  y: integer\n"
        "end;\n"
        "var p2: Pair(3);\n"
        "begin\n"
        "  p2.x[1] := 7;\n"
        "  p2.y := 42;\n"
        "  writeln(p2.x[1]);\n"
        "  writeln(p2.y);\n"
        "  writeln(p2.n)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n42\n3\n");
}

// EP §6.4.7: a field of a record body may be bounded by a discriminant.  The
// extent is a constant in each instance, but not in the declaration, and a
// bound that would not fold used to be read as zero — which made `array[0..n]`
// one element long, small enough that everything written past the first ran
// off the end of the variable.
TEST(EP7Schema, ARecordBodyGivesEachFieldItsDiscriminantExtent) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type poly(n: integer) = record\n"
        "  deg: integer;\n"
        "  c: array[0..n] of real\n"
        "end;\n"
        "var q: poly(2); i: integer;\n"
        "begin\n"
        "  q.deg := 2;\n"
        "  for i := 0 to 2 do q.c[i] := i + 0.5;\n"
        "  writeln(q.deg:0, ' ', q.c[0]:0:1, ' ', q.c[2]:0:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 0.5 2.5\n");
}

// One declaration serves every instantiation, so a layout worked out from the
// declaration alone is whichever instance reached it first: `poly(5)` took the
// three elements of `poly(2)` and wrote its last two into the next variable.
TEST(EP7Schema, TwoInstancesOfARecordSchemaAreLaidOutApart) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type poly(n: integer) = record\n"
        "  deg: integer;\n"
        "  c: array[0..n] of real\n"
        "end;\n"
        "var big: poly(5); small: poly(2); i: integer;\n"
        "begin\n"
        "  small.deg := 2; big.deg := 5;\n"
        "  for i := 0 to 2 do small.c[i] := i;\n"
        "  for i := 0 to 5 do big.c[i] := 100 + i;\n"
        "  writeln(small.deg:0, ' ', small.c[2]:0:1);\n"
        "  writeln(big.deg:0, ' ', big.c[5]:0:1)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2 2.0\n5 105.0\n");
}

// The discriminants reach every extent in the body, however deeply it is
// written: several dimensions at once, an expression over more than one
// discriminant, and a record nested inside the body.
TEST(EP7Schema, ARecordBodyIsMeasuredThroughoutByItsDiscriminants) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type mat(r, c: integer) = record\n"
        "  m: array[1..r, 1..c] of integer;\n"
        "  edge: array[1..2*r+1] of integer;\n"
        "  inner: record w: array[1..c] of integer end\n"
        "end;\n"
        "var a: mat(2, 3); b: mat(4, 1); i, j: integer;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a.m[i, j] := i * 10 + j;\n"
        "  for i := 1 to 5 do a.edge[i] := i;\n"
        "  for i := 1 to 3 do a.inner.w[i] := 100 + i;\n"
        "  for i := 1 to 4 do b.m[i, 1] := i;\n"
        "  for i := 1 to 9 do b.edge[i] := i * 2;\n"
        "  b.inner.w[1] := 7;\n"
        "  writeln(a.m[2, 3]:0, ' ', a.edge[5]:0, ' ', a.inner.w[3]:0);\n"
        "  writeln(b.m[4, 1]:0, ' ', b.edge[9]:0, ' ', b.inner.w[1]:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "23 5 103\n4 18 7\n");
}

// ISO §6.4.3.3: the alternatives of a variant share one run of storage, and
// what that run has to hold is a question the discriminants answer too.
TEST(EP7Schema, AVariantInARecordSchemaIsSizedByTheDiscriminant) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type vrec(n: integer) = record\n"
        "  w: array[0..n] of integer;\n"
        "  case kind: boolean of\n"
        "    true:  (many: array[0..n] of integer);\n"
        "    false: (one: integer)\n"
        "end;\n"
        "var a: vrec(1); b: vrec(4); i: integer;\n"
        "begin\n"
        "  a.kind := true; b.kind := true;\n"
        "  for i := 0 to 1 do begin a.w[i] := i + 10; a.many[i] := i + 50 end;\n"
        "  for i := 0 to 4 do begin b.w[i] := i + 100; b.many[i] := i + 500 end;\n"
        "  writeln(a.w[1]:0, ' ', a.many[1]:0);\n"
        "  writeln(b.w[4]:0, ' ', b.many[4]:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 51\n104 504\n");
}

// A local and a heap instance are laid out by the same reckoning as a global.
TEST(EP7Schema, ARecordSchemaIsMeasuredTheSameWhereverItLives) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type box(n: integer) = record w: array[0..n] of integer; tail: integer end;\n"
        "type pbox = ^box(3);\n"
        "var ptr: pbox; i: integer;\n"
        "procedure local;\n"
        "var l3: box(3); l2: box(2); j: integer;\n"
        "begin\n"
        "  for j := 0 to 3 do l3.w[j] := 300 + j;\n"
        "  for j := 0 to 2 do l2.w[j] := 200 + j;\n"
        "  l3.tail := 33; l2.tail := 22;\n"
        "  writeln(l3.w[3]:0, ' ', l3.tail:0, ' ', l2.w[2]:0, ' ', l2.tail:0)\n"
        "end;\n"
        "begin\n"
        "  local;\n"
        "  new(ptr);\n"
        "  for i := 0 to 3 do ptr^.w[i] := i * 7;\n"
        "  ptr^.tail := 77;\n"
        "  writeln(ptr^.w[3]:0, ' ', ptr^.tail:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "303 33 202 22\n21 77\n");
}

// Schema with two discriminants.
TEST(EP7Schema, TwoDiscriminants) {
    auto R = compileAndRun(
        "program p;\n"
        "type Mat(m: integer; n: integer) = array[1..m] of array[1..n] of real;\n"
        "var A: Mat(2, 3);\n"
        "begin\n"
        "  A[1][2] := 5.0;\n"
        "  writeln(A.m);\n"
        "  writeln(A.n);\n"
        "  writeln(A[1][2]:1:0)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n3\n5\n");
}

// ---------------------------------------------------------------------------
// EP §6.8.7 Structured value constructors (Tier 8)
// ---------------------------------------------------------------------------

// §6.8.7.2 Array value constructors

TEST(EP8ArrayConstructor, AllIndicesSpecified) {
    // Row[1: 10; 2: 20; 3: 30; 4: 40; 5: 50] assigns all five elements.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1: 10; 2: 20; 3: 30; 4: 40; 5: 50];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n40\n50\n");
}

TEST(EP8ArrayConstructor, OtherwiseDefault) {
    // Row[otherwise: 99] fills every element with 99.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..4] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[otherwise: 99];\n"
        "  for i := 1 to 4 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "99\n99\n99\n99\n");
}

TEST(EP8ArrayConstructor, MixedIndexAndOtherwise) {
    // Set index 2 to 42, all others to 0 (zero-init then override).
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[2: 42; otherwise: 0];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n42\n0\n0\n0\n");
}

TEST(EP8ArrayConstructor, RangeIndex) {
    // Row[1..3: 7; 4..5: 9] uses range labels.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1..3: 7; 4..5: 9];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n7\n7\n9\n9\n");
}

TEST(EP8ArrayConstructor, MultiLabelArm) {
    // Row[1,3: 0; 2: 42; 4,5: 99] uses comma-separated index lists.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..5] of integer;\n"
        "var r: Row;\n"
        "    i: integer;\n"
        "begin\n"
        "  r := Row[1,3: 0; 2: 42; 4,5: 99];\n"
        "  for i := 1 to 5 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n42\n0\n99\n99\n");
}

// §6.8.7.3 Record value constructors

TEST(EP8RecordConstructor, AllFields) {
    // Point[x: 10; y: 20] assigns both fields.
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var p: Point;\n"
        "begin\n"
        "  p := Point[x: 10; y: 20];\n"
        "  writeln(p.x);\n"
        "  writeln(p.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n");
}

TEST(EP8RecordConstructor, PartialFields) {
    // Point[x: 7] leaves y as zero (zero-initialized).
    auto R = compileAndRun(
        "program p;\n"
        "type Point = record x, y: integer end;\n"
        "var pt: Point;\n"
        "begin\n"
        "  pt := Point[x: 7];\n"
        "  writeln(pt.x);\n"
        "  writeln(pt.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n0\n");
}

TEST(EP8ArrayConstructor, CompleterNeedsNoColon) {
    // §6.8.7.2: array-value-completer = 'otherwise' component-value.  plang
    // also tolerates a colon there, so both spellings must work.
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..4] of integer;\n"
        "var a, b: row;\n"
        "    i: integer;\n"
        "begin\n"
        "  a := row[1: 10; otherwise 0];\n"
        "  b := row[1: 10; otherwise: 0];\n"
        "  for i := 1 to 4 do write(a[i], b[i], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1010 00 00 00 \n");
}

TEST(EP8RecordConstructor, VariantPart) {
    // §6.8.7.3: variant-part-value = 'case' [ tag-field-identifier ':' ]
    //   constant-tag-value 'of' '[' field-list-value ']'.  The tag value is a
    //   component of the record like any other.
    auto R = compileAndRun(
        "program p;\n"
        "type shape = record\n"
        "  area: integer;\n"
        "  case kind: 1..2 of\n"
        "    1: (side: integer);\n"
        "    2: (w, h: integer)\n"
        "end;\n"
        "var s: shape;\n"
        "begin\n"
        "  s := shape[area: 9; case kind: 1 of [side: 3]];\n"
        "  writeln(s.area, ' ', s.kind, ' ', s.side);\n"
        "  s := shape[area: 12; case kind: 2 of [w: 3; h: 4]];\n"
        "  writeln(s.area, ' ', s.kind, ' ', s.w, ' ', s.h)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 1 3\n12 2 3 4\n");
}

TEST(EP8StructuredConst, ArrayValueInAConstantDefinition) {
    // §6.8.7 exists largely so that a structured value can be a constant, so
    // the constant has to be able to name a type defined above it.
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..4] of integer;\n"
        "const v = row[1: 10; 2..3: 20; otherwise 0];\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 1 to 4 do write(v[i], ' ');\n"
        "  writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10 20 20 0 \n");
}

TEST(EP8StructuredConst, RecordValueAndALocalOne) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "     pt  = record x, y: integer end;\n"
        "const origin = pt[x: 3; y: 4];\n"
        "procedure show;\n"
        "const v = row[1: 7; otherwise 1];\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do write(v[i], ' ');\n"
        "  writeln\n"
        "end;\n"
        "begin\n"
        "  show;\n"
        "  writeln(origin.x + origin.y)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1 1 \n7\n");
}

TEST(EP8StructuredConst, IsStillNotAssignable) {
    auto R = compileAndRun(
        "program p;\n"
        "type row = array[1..3] of integer;\n"
        "const v = row[otherwise 1];\n"
        "begin v[1] := 9 end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not an assignable variable"), std::string::npos)
        << R.Stderr;
}

// §6.8.7.4 Set value constructors with type-name prefix

TEST(EP8SetConstructor, TypedSetLiteralMultiElement) {
    // Colors[red, green] — typed set literal with two elements.
    auto R = compileAndRun(
        "program p;\n"
        "type Color = (red, green, blue);\n"
        "type Colors = set of Color;\n"
        "var c: Colors;\n"
        "begin\n"
        "  c := Colors[red, green];\n"
        "  if red in c then writeln('red');\n"
        "  if green in c then writeln('green');\n"
        "  if not (blue in c) then writeln('no blue')\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "red\ngreen\nno blue\n");
}

// §6.4.1 'value' initial state specifier

TEST(EP8ValueInit, ScalarInitializer) {
    // var x: integer value 42;
    auto R = compileAndRun(
        "program p;\n"
        "var x: integer value 42;\n"
        "begin\n"
        "  writeln(x)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(EP8ValueInit, ArrayWithConstructorInit) {
    // var r: Row value Row[otherwise: 5]; — array with constructor init.
    auto R = compileAndRun(
        "program p;\n"
        "type Row = array[1..3] of integer;\n"
        "var r: Row value Row[1: 10; 2: 20; 3: 30];\n"
        "    i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do writeln(r[i])\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n20\n30\n");
}

TEST(EP8ValueInit, GlobalVarInit) {
    // Global variable with value initializer.
    auto R = compileAndRun(
        "program p;\n"
        "var counter: integer value 100;\n"
        "begin\n"
        "  counter := counter + 1;\n"
        "  writeln(counter)\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "101\n");
}

TEST(EP8ValueInit, LocalVarInit) {
    // Local variable with value initializer inside a procedure.
    auto R = compileAndRun(
        "program p;\n"
        "function add(a, b: integer): integer;\n"
        "var result: integer value 0;\n"
        "begin\n"
        "  result := a + b;\n"
        "  add := result\n"
        "end;\n"
        "begin\n"
        "  writeln(add(3, 4))\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

// ---------------------------------------------------------------------------
// EP Tier 11 — Date & Time
// ---------------------------------------------------------------------------

static const std::string kEP11 = "-std=iso10206";

// §6.4.3.4: TimeStamp is a predefined record type with 8 fields
TEST(EP11DateTime, TimeStampFieldsAccessible) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := true;\n"
        "  t.year := 2025; t.month := 6; t.day := 15;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(t.year, ' ', t.month, ' ', t.day)\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2025 6 15\n");
}

// §6.7.6.9: date(t) formats as YYYY-MM-DD
TEST(EP11DateTime, DateFormatsCorrectly) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := true;\n"
        "  t.year := 2026; t.month := 8; t.day := 8;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(date(t))\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2026-08-08\n");
}

// §6.7.6.9: time(t) formats as HH:MM:SS
TEST(EP11DateTime, TimeFormatsCorrectly) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := false;\n"
        "  t.year := 0; t.month := 0; t.day := 0;\n"
        "  t.TimeValid := true;\n"
        "  t.hour := 14; t.minute := 30; t.second := 5;\n"
        "  writeln(time(t))\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "14:30:05\n");
}

// §6.7.6.9: invalid DateValid/TimeValid yields zeroed output
TEST(EP11DateTime, InvalidDateGivesZeroes) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  t.DateValid := false;\n"
        "  t.year := 0; t.month := 0; t.day := 0;\n"
        "  t.TimeValid := false;\n"
        "  t.hour := 0; t.minute := 0; t.second := 0;\n"
        "  writeln(date(t)); writeln(time(t))\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0000-00-00\n00:00:00\n");
}

// §6.7.5.8: GetTimeStamp fills the record; DateValid and year >= 2024
TEST(EP11DateTime, GetTimeStampFillsRecord) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  if t.DateValid then writeln('date ok') else writeln('date fail');\n"
        "  if t.year >= 2024 then writeln('year ok') else writeln('year fail');\n"
        "  if t.TimeValid then writeln('time ok') else writeln('time fail')\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "date ok\nyear ok\ntime ok\n");
}

// §6.7.5.8 + §6.7.6.9 combined: GetTimeStamp then format
TEST(EP11DateTime, GetTimeStampThenFormat) {
    // Just verify the formatted strings have the right length (10 and 8)
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  writeln(length(date(t)));\n"
        "  writeln(length(time(t)))\n"
        "end.\n", kEP11);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "10\n8\n");
}

// ---------------------------------------------------------------------------
// EP Tier 12 — Binding System
// ---------------------------------------------------------------------------

static const std::string kEP12 = "-std=iso10206";

// §6.4.3.4: BindingType has required 'bound' field accessible
TEST(EP12Binding, BindingTypeFieldsAccessible) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: BindingType;\n"
        "begin\n"
        "  b.bound := true;\n"
        "  if b.bound then writeln('bound') else writeln('unbound');\n"
        "  b.bound := false;\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\nunbound\n");
}

// §6.7.6.8: binding(f) after bind(f,b) reports the entity named by b.name.
// §6.7.5.6 NOTE 3: b.bound is ignored by bind, so the name is what binds.
TEST(EP12Binding, BindingAfterExplicitBind) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_explicit.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound');\n"
        "  writeln(b2.name)\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\n/tmp/plang_bind_explicit.txt\n");
}

// §6.7.5.6 NOTE 3: b.bound is ignored, so binding to an unnamed entity does
// not make the file bound.
TEST(EP12Binding, BoundFieldIsIgnoredByBind) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  b.bound := true;\n"
        "  bind(f, b);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// §6.7.5.6: unbind(f) causes binding(f).bound = false
TEST(EP12Binding, UnbindClearsBinding) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  unbind(f);\n"
        "  b := binding(f);\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// §6.7.5.6: bind(f, b) re-establishes binding after unbind
TEST(EP12Binding, BindReestablishesBinding) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b, b2: BindingType;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  unbind(f);\n"
        "  b.name := '/tmp/plang_bind_reestablish.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f);\n"
        "  b2 := binding(f);\n"
        "  if b2.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bound\n");
}

// §6.7.5.6: a bound file opens the named entity, so data written through it
// survives close and is read back by a plain reset.
TEST(EP12Binding, BoundFileRoundTrips) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType; s: string(40);\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_roundtrip.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f); writeln(f, 'through the binding'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "through the binding\n");
}

// §6.7.6.8: binding(f) on a closed file returns bound=false
TEST(EP12Binding, BindingOnClosedFileReturnsUnbound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; b: BindingType;\n"
        "begin\n"
        "  b := binding(f);\n"
        "  if b.bound then writeln('bound') else writeln('unbound')\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "unbound\n");
}

// EP §6.4.1: only a variable declared bindable may be bound.  Before, the
// qualifier was stripped by the parser and any file at all was accepted.
TEST(EP12Binding, APlainFileVariableCannotBeBound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; b: BindingType;\n"
        "begin b.name := '/tmp/plang_notbindable.txt'; bind(f, b) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not declared bindable"), std::string::npos);
}

TEST(EP12Binding, UnbindAndBindingAlsoRequireABindableVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; b: BindingType;\n"
        "begin unbind(f); b := binding(f) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
}

// A named type carries the qualifier to every variable declared with it.
TEST(EP12Binding, ANamedBindableTypeDeclaresBindableVariables) {
    auto R = compileAndRun(
        "program p;\n"
        "type bf = bindable text;\n"
        "var f: bf; b: BindingType; s: string(30);\n"
        "begin\n"
        "  b.name := '/tmp/plang_bind_namedtype.txt';\n"
        "  bind(f, b);\n"
        "  rewrite(f); writeln(f, 'named'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "named\n");
}

// EP §6.7.5.6: the first argument is a variable, not any expression that
// happens to have a file type.
TEST(EP12Binding, TheFirstArgumentMustBeAVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: BindingType;\n"
        "begin bind(42, b) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("must be a variable"), std::string::npos);
}

TEST(EP12Binding, TheSecondArgumentMustBeABindingType) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; n: integer;\n"
        "begin bind(f, n) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("BindingType"), std::string::npos);
}

TEST(EP12Binding, TheArgumentCountIsChecked) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text;\n"
        "begin bind(f) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
}

// EP §6.4.1 lets any type be bindable and leaves what that means to the
// implementation; plang binds files to paths and says so for anything else.
TEST(EP12Binding, ABindableVariableThatIsNotAFileIsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "var n: bindable integer; b: BindingType;\n"
        "begin b.name := '/tmp/x'; bind(n, b) end.\n", kEP12);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("only a file variable"), std::string::npos);
}

// A bindable var parameter stands for the bindable variable passed to it.
TEST(EP12Binding, ABindableVarParameterCanBeBound) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: bindable text; s: string(30);\n"
        "procedure attach(var g: bindable text; path: string(60));\n"
        "var b: BindingType;\n"
        "begin b.name := path; bind(g, b) end;\n"
        "begin\n"
        "  attach(f, '/tmp/plang_bind_varparam.txt');\n"
        "  rewrite(f); writeln(f, 'via parameter'); close(f);\n"
        "  reset(f); readln(f, s); close(f);\n"
        "  writeln(s)\n"
        "end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "via parameter\n");
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
        "end.\n", kEP12);
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
        "begin writeln(origin.x + origin.y, ' ', origin().x) end.\n", kEP12);
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
        "begin writeln(ramp[3], ' ', head^) end.\n", kEP12);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9 9\n");
}

// ---------------------------------------------------------------------------
// EP §6.11 — Tier 13: Modules (driver/codegen tests)
// ---------------------------------------------------------------------------

static const std::string kEP13 = "-std=iso10206";

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
        "end.\n", kEP13);
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
        "end.\n", kEP13);
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
        "end.\n", kEP13);
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
        "begin writeln(f(1)) end.\n", kEP13);
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
        "begin writeln(f(1)) end.\n", kEP13);
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
        "begin writeln(f(1)) end.\n", kEP13);
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
        "begin writeln(n) end.\n", kEP13);
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
        "begin writeln(1) end.\n", kEP13);
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
        "begin c := green; writeln(code(c)) end.\n", kEP13);
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
        "begin writeln(two) end.\n", kEP13);
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
        "begin writeln(one) end.\n", kEP13);
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
        "begin bump; bump; x[1] := count; writeln(x[1]) end.\n", kEP13);
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
        "begin writeln(outer(7)) end.\n", kEP13);
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
        "begin writeln('body') end.\n", kEP13);
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
        "begin q end.\n", kEP13);
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
        "begin q.a := 1; q.b := 2; writeln(sum(q)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
    EXPECT_EQ(R.Stderr, "") << R.Stderr;
}

// ---------------------------------------------------------------------------
// Separate compilation tests — PMI write/read cycle and multi-file builds
// ---------------------------------------------------------------------------

/// Compile a module source to object + PMI, then compile a program that
/// imports the module and link them together.  Returns the program's output.
struct TwoFileResult {
    int         ExitCode{-1};
    std::string Stdout;
    std::string Stderr;
};

static TwoFileResult compileTwoFiles(const std::string& ModSrc,
                                      const std::string& ProgSrc,
                                      const std::string& ExtraFlags = "") {
    TwoFileResult R;

    // Both sources go in a directory of this case's own: the interface file is
    // written beside the module under the module's name, and two cases that
    // name their modules alike would otherwise read each other's.
    const std::string TmpDir   = makeTempDir();
    const std::string ModFile  = TmpDir + "/mod.pas";
    const std::string ProgFile = TmpDir + "/prog.pas";
    if (!writeFileAt(ModFile, ModSrc) || !writeFileAt(ProgFile, ProgSrc)) {
        R.Stderr = "could not create temp files";
        removeTempDir(TmpDir);
        return R;
    }

    // Step 1: Compile the module to a .o file.
    // The frontend also writes ModuleName.pmi alongside the source file.
    std::string ModObj = ModFile.substr(0, ModFile.size() - 4) + ".o";
    char ErrTmpl1[] = "/tmp/plang_sep_XXXXXX.err";
    int  ErrFd1     = mkstemps(ErrTmpl1, 4);
    close(ErrFd1);
    {
        std::string Cmd = std::string(PLANG_PATH)
            + " " + ExtraFlags
            + " -c " + ModFile
            + " -o " + ModObj
            + " 2>" + ErrTmpl1;
        int Rc = std::system(Cmd.c_str());
        R.Stderr += runCmd(std::string("cat ") + ErrTmpl1);
        std::remove(ErrTmpl1);
        if (Rc != 0) {
            R.ExitCode = Rc;
            removeTempDir(TmpDir);
            return R;
        }
    }

    // Step 2: Compile the program and link it with the module object.
    // Pass ModObj as an extra argument: the driver routes .o files to the linker.
    // The -I flag points to TmpDir so the importer finds the .pmi file.
    char BinTmpl[] = "/tmp/plang_sep_XXXXXX";
    int  BinFd     = mkstemp(BinTmpl);
    close(BinFd);
    char ErrTmpl2[] = "/tmp/plang_sep_XXXXXX.err";
    int  ErrFd2     = mkstemps(ErrTmpl2, 4);
    close(ErrFd2);
    {
        std::string Cmd = std::string(PLANG_PATH)
            + " " + ExtraFlags
            + " -I" + TmpDir
            + " " + ProgFile
            + " " + ModObj
            + " -o " + BinTmpl
            + " 2>" + ErrTmpl2;
        int Rc = std::system(Cmd.c_str());
        R.Stderr += runCmd(std::string("cat ") + ErrTmpl2);
        std::remove(ErrTmpl2);
        if (Rc != 0) {
            R.ExitCode = Rc;
            removeTempDir(TmpDir);
            std::remove(BinTmpl);
            return R;
        }
    }

    // Step 3: Run the resulting binary.
    char RunErrTmpl[] = "/tmp/plang_sep_XXXXXX.rer";
    int  RunErrFd     = mkstemps(RunErrTmpl, 4);
    close(RunErrFd);
    R.Stdout = runCmd(std::string(BinTmpl) + " < /dev/null 2>" + RunErrTmpl + "; echo \"exit:$?\"");
    R.ExitCode = 0;
    if (auto Pos = R.Stdout.rfind("exit:"); Pos != std::string::npos) {
        R.ExitCode = std::atoi(R.Stdout.c_str() + Pos + 5);
        R.Stdout.erase(Pos);
    }
    R.Stderr += runCmd(std::string("cat ") + RunErrTmpl);
    std::remove(RunErrTmpl);

    // Cleanup.
    removeTempDir(TmpDir);
    std::remove(BinTmpl);
    return R;
}

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

// A record whose size varies with its discriminants would need run-time field
// offsets; say so rather than lowering it wrongly.
TEST(Schema, VaryingRecordBodyIsRejectedWithAReason) {
    auto R = compileAndRun(
        "program p;\n"
        "type buf(n: integer) = record len: integer; d: array[1..n] of char end;\n"
        "var q: ^buf;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("size varies"), std::string::npos) << R.Stderr;
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
// EP §6.4.3.4 — writing the TimeStamp flags
//
// write dispatched on the LLVM type alone.  TimeStamp holds DateValid and
// TimeValid as i8 so the record matches its C counterpart byte for byte, and
// at that width a boolean is indistinguishable from a char, so both were
// written as character 1 — which shows up as nothing at all.
// ---------------------------------------------------------------------------

TEST(WriteBoolean, TimeStampFlags) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin\n"
        "  GetTimeStamp(t);\n"
        "  writeln('DateValid=', t.DateValid, ' TimeValid=', t.TimeValid)\n"
        "end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "DateValid=true TimeValid=true\n");
}

TEST(WriteBoolean, TimeStampFlagWithAFieldWidth) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: TimeStamp;\n"
        "begin GetTimeStamp(t); writeln('[', t.DateValid:7, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[   true]\n");
}

TEST(WriteBoolean, CharsAreStillWrittenAsChars) {
    // The two share an LLVM type, so the fix has to keep them apart.
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := 'A'; writeln(c, ' ', c:3) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A   A\n");
}

TEST(WriteBoolean, OrdinaryBooleansAreUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record flag: boolean end;\n"
        "var b: boolean; x: r;\n"
        "begin b := true; x.flag := false;\n"
        "  writeln(b, ' ', x.flag, ' ', b:6) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false   true\n");
}

// ---------------------------------------------------------------------------
// Strings that are not string variables
//
// A string value is the address of a { length, bytes } struct, and three
// places built one that was not: the 'value' initializer stored the pointer to
// the literal's temporary straight into the variable, a named string constant
// was interned as a bare run of bytes whose first eight characters were then
// read as its length, and a string argument passed by value was handed over as
// an address where the callee had declared the struct itself.
// ---------------------------------------------------------------------------

TEST(StringValueInit, GlobalStringInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10) value 'hi';\n"
        "begin writeln('[', s, '] ', length(s)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi] 2\n");
}

TEST(StringValueInit, LocalStringInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; var s: string(10) value 'local';\n"
        "begin writeln('[', s, ']') end;\n"
        "begin q end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[local]\n");
}

TEST(StringValueInit, TwoNamesShareOneInitializer) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: string(8) value 'xy';\n"
        "begin writeln('[', a, '][', b, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xy][xy]\n");
}

TEST(StringValueInit, TheVariableIsStillWritable) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10) value 'hi';\n"
        "begin writeln(s); s := 'there'; writeln(s) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi\nthere\n");
}

TEST(StringValueInit, ThroughANamedType) {
    // Whether a declaration is a string is a question about the type, not
    // about how it was written; keying off the syntax missed this one and left
    // the pointer bits in the length field.
    auto R = compileAndRun(
        "program p;\n"
        "type st = string(12);\n"
        "var a: st value 'init';\n"
        "begin writeln('a=[', a, '] len=', length(a)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a=[init] len=4\n");
}

TEST(StringValueInit, ThroughANamedTypeInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "type st = string(12);\n"
        "procedure q; var a: st value 'init';\n"
        "begin writeln('[', a, ']') end;\n"
        "begin q end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[init]\n");
}

TEST(StringValueInit, NonStringInitializersAreUnaffected) {
    auto R = compileAndRun(
        "program p;\n"
        "var n: integer value 7; r: real value 1.5;\n"
        "begin writeln(n, ' ', r:0:1) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1.5\n");
}

TEST(StringConst, AssignedToAStringVariable) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(10);\n"
        "begin s := greeting; writeln('[', s, '] ', length(s)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello] 5\n");
}

TEST(StringConst, WrittenDirectly) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "begin writeln('[', greeting, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello]\n");
}

TEST(StringConst, Concatenated) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(20);\n"
        "begin s := greeting + ' there'; writeln('[', s, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello there]\n");
}

TEST(StringConst, Compared) {
    auto R = compileAndRun(
        "program p;\n"
        "const greeting = 'hello';\n"
        "var s: string(10);\n"
        "begin s := 'hello'; writeln(s = greeting) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\n");
}

TEST(StringConst, DeclaredInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; const tag = 'inner'; var s: string(10);\n"
        "begin s := tag; writeln('[', s, ']') end;\n"
        "begin q end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[inner]\n");
}

TEST(StringConst, ASingleCharacterConstantIsStillAChar) {
    auto R = compileAndRun(
        "program p;\n"
        "const c = 'x';\n"
        "var ch: char;\n"
        "begin ch := c; writeln('[', ch, '] ', ord(ch)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[x] 120\n");
}

TEST(StringParam, PassedByValue) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin s := 'hi'; show(s) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

TEST(StringParam, ArgumentCapacityNeedNotMatchTheParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(10);\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin s := 'hi'; show(s) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

TEST(StringParam, LiteralAndConstantArguments) {
    auto R = compileAndRun(
        "program p;\n"
        "const g = 'hello';\n"
        "procedure show(x: string(20)); begin writeln('[', x, ']') end;\n"
        "begin show('hi'); show(g) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n[hello]\n");
}

TEST(StringParam, ByValueIsACopy) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure show(x: string(20));\n"
        "begin x := 'changed'; writeln('[', x, ']') end;\n"
        "begin s := 'orig'; show(s); writeln('[', s, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[changed]\n[orig]\n");
}

TEST(StringParam, VarParameterStillAliases) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "procedure setit(var x: string(20)); begin x := 'set' end;\n"
        "begin s := 'orig'; setit(s); writeln('[', s, ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[set]\n");
}

TEST(StringParam, ToAFunctionAndAlongsideOthers) {
    auto R = compileAndRun(
        "program p;\n"
        "var s: string(20);\n"
        "function len2(x: string(20)): integer;\n"
        "begin len2 := length(x) * 2 end;\n"
        "procedure both(a: string(10); b: string(10));\n"
        "begin writeln('[', a, '][', b, ']') end;\n"
        "begin s := 'abc'; writeln(len2(s)); both('one', 'two') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n[one][two]\n");
}

// ---------------------------------------------------------------------------
// ISO §6.6.1 — forward declarations
//
// A 'forward' declaration exists so that a procedure can be called before its
// body appears, which is what makes mutual recursion expressible.  Neither
// half of that worked: the call site, reaching a name with no function behind
// it yet, invented a declaration from the shape of the argument list, and the
// definition then found the name taken and was renamed to plang_b.1, leaving
// 'undefined symbol: plang_b' at link time.  Separately, the standard spelling
// of the defining occurrence — the name alone, repeating neither the parameter
// list nor the result type — was rejected outright.
// ---------------------------------------------------------------------------

TEST(ForwardDecl, MutualRecursionLinks) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "procedure b(n: integer); begin write(n); a(n) end;\n"
        "begin a(3); writeln('.') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "210.\n");
}

TEST(ForwardDecl, MutuallyRecursiveFunctions) {
    auto R = compileAndRun(
        "program p;\n"
        "function isodd(n: integer): boolean; forward;\n"
        "function iseven(n: integer): boolean;\n"
        "begin if n = 0 then iseven := true else iseven := isodd(n - 1) end;\n"
        "function isodd(n: integer): boolean;\n"
        "begin if n = 0 then isodd := false else isodd := iseven(n - 1) end;\n"
        "begin writeln(iseven(10), ' ', isodd(7)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true\n");
}

TEST(ForwardDecl, AVarParameterSurvivesTheForwardCall) {
    // The invented declaration used to be built from the argument's type, so a
    // var parameter came out as i64 where the definition wanted ptr.
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure b(var x: integer); forward;\n"
        "procedure a(var x: integer); begin b(x) end;\n"
        "procedure b(var x: integer); begin x := 42 end;\n"
        "begin v := 0; a(v); writeln(v) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, NestedInsideAProcedure) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure outer;\n"
        "  var k: integer;\n"
        "  procedure b(n: integer); forward;\n"
        "  procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "  procedure b(n: integer); begin k := k + 1; a(n) end;\n"
        "begin k := 0; a(4); writeln(k) end;\n"
        "begin outer end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4\n");
}

TEST(ForwardDecl, IsoFormOmitsTheParameterList) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure a(n: integer); begin if n > 0 then b(n - 1) end;\n"
        "procedure b;\n"
        "begin write(n); a(n) end;\n"
        "begin a(3); writeln('.') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "210.\n");
}

TEST(ForwardDecl, IsoFormOmitsTheResultTypeToo) {
    auto R = compileAndRun(
        "program p;\n"
        "function g(n: integer): integer; forward;\n"
        "function f(n: integer): integer; begin f := g(n) + 1 end;\n"
        "function g;\n"
        "begin g := n * 2 end;\n"
        "begin writeln(f(5)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11\n");
}

TEST(ForwardDecl, IsoFormWithAVarParameter) {
    auto R = compileAndRun(
        "program p;\n"
        "var v: integer;\n"
        "procedure b(var x: integer); forward;\n"
        "procedure a(var x: integer); begin b(x) end;\n"
        "procedure b;\n"
        "begin x := 42 end;\n"
        "begin v := 0; a(v); writeln(v) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, RepeatingTheHeadingIsStillAccepted) {
    // Not what ISO §6.6.1 asks for, but what most Pascals allow and what this
    // compiler has always taken.
    auto R = compileAndRun(
        "program p;\n"
        "function g(n: integer): integer; forward;\n"
        "function g(n: integer): integer;\n"
        "begin g := n * 2 end;\n"
        "begin writeln(g(21)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(ForwardDecl, ARepeatedHeadingThatDisagreesIsRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure b(n: integer); forward;\n"
        "procedure b(n: real);\n"
        "begin writeln(n:0:1) end;\n"
        "begin b(1.0) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not match forward declaration"),
              std::string::npos) << R.Stderr;
}

TEST(ForwardDecl, AParameterlessProcedureIsNotTreatedAsForward) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure q; begin writeln('q') end;\n"
        "begin q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "q\n");
}

TEST(ForwardDecl, FunctionWithoutAResultTypeAndNoForwardDeclaration) {
    // The parser now carries this heading through, since only Sema can tell it
    // from the ISO defining occurrence above.  It must still be rejected, and
    // with a diagnostic rather than an internal error from codegen.
    auto R = compileAndRun(
        "program p;\n"
        "function f;\n"
        "begin f := 1 end;\n"
        "begin writeln(f) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("has no result type"), std::string::npos) << R.Stderr;
    EXPECT_EQ(R.Stderr.find("internal error"), std::string::npos) << R.Stderr;
}

TEST(ForwardDecl, ConformantArrayParameterThroughAForwardCall) {
    auto R = compileAndRun(
        "program p;\n"
        "var arr: array[1..3] of integer;\n"
        "function total(var a: array[lo..hi: integer] of integer): integer;"
        " forward;\n"
        "function go(var a: array[lo..hi: integer] of integer): integer;\n"
        "begin go := total(a) end;\n"
        "function total;\n"
        "var i, s: integer;\n"
        "begin s := 0; for i := lo to hi do s := s + a[i]; total := s end;\n"
        "begin arr[1] := 1; arr[2] := 2; arr[3] := 3; writeln(go(arr)) end.\n",
        kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

// ---------------------------------------------------------------------------
// ISO §6.4.2.2 ordinal comparison.  Every ordinal is ordered by its ordinal
// number, which is never negative — but LLVM reads an i1 holding 'true' as -1
// under a signed compare, so booleans came out reversed and a boolean for-loop
// wrapped instead of terminating.
// ---------------------------------------------------------------------------

TEST(OrdinalCompare, BooleansOrderFalseBeforeTrue) {
    auto R = compileAndRun(
        "program p;\n"
        "var t, f: boolean;\n"
        "begin t := true; f := false;\n"
        " writeln(f < t, ' ', f <= t, ' ', t > f, ' ', t >= f);\n"
        " writeln(t < f, ' ', t <= f, ' ', f > t, ' ', f >= t) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true true true true\nfalse false false false\n");
}

TEST(OrdinalCompare, CharsAboveTheSignedRangeCompareHigh) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := chr(200); writeln(c > 'a', ' ', c < 'a') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(OrdinalCompare, EnumerationsKeepDeclarationOrder) {
    auto R = compileAndRun(
        "program p;\n"
        "type col = (red, green, blue);\n"
        "var a, b: col;\n"
        "begin a := red; b := blue; writeln(a < b, ' ', b < a) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(OrdinalCompare, NegativeIntegersStillCompareSigned) {
    auto R = compileAndRun(
        "program p;\n"
        "var a, b: integer;\n"
        "begin a := -5; b := 3; writeln(a < b, ' ', a > b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(ForLoop, OverBooleanRunsTwiceAndTerminates) {
    auto R = compileAndRun(
        "program p;\n"
        "var b: boolean;\n"
        "begin for b := false to true do write(b, ' '); writeln('done') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "false true done\n");
}

TEST(ForLoop, OverCharSubrangeVisitsEachLetter) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin for c := 'a' to 'e' do write(c); writeln end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcde\n");
}

TEST(ForLoop, ToMaxintTerminates) {
    // ISO §6.8.3.9: the control variable is never advanced past the final
    // value.  Incrementing first and comparing afterwards overflows here and
    // the loop never ends.
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := maxint - 2 to maxint do n := n + 1;\n"
        " writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3\n");
}

TEST(ForLoop, DowntoFromMinintTerminates) {
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := -maxint + 1 downto -maxint do n := n + 1;\n"
        " writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(ForLoop, EmptyRangeRunsZeroTimes) {
    auto R = compileAndRun(
        "program p;\n"
        "var i, n: integer;\n"
        "begin n := 0; for i := 5 to 1 do n := n + 1; writeln(n) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0\n");
}

// ---------------------------------------------------------------------------
// Integer-to-real widening on assignment.  The destination decides whether a
// conversion is needed, so reading the type off the source stored the integer
// bit pattern into every double that was not a bare variable.
// ---------------------------------------------------------------------------

TEST(IntToReal, WidensIntoAnArrayElement) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..3] of real;\n"
        "begin a[1] := 5; writeln(a[1]:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0000\n");
}

TEST(IntToReal, WidensIntoARecordField) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var q: r;\n"
        "begin q.x := 5; writeln(q.x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "5.0000\n");
}

TEST(IntToReal, WidensIntoANestedFieldOfAnArrayElement) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var a: array[1..2] of r;\n"
        "begin a[2].x := 7; writeln(a[2].x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7.0000\n");
}

TEST(IntToReal, WidensThroughAPointerDereference) {
    auto R = compileAndRun(
        "program p;\n"
        "type pr = ^real;\n"
        "var p: pr;\n"
        "begin new(p); p^ := 9; writeln(p^:0:4); dispose(p) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "9.0000\n");
}

TEST(IntToReal, WidensInsideAWithStatement) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: real end;\n"
        "var q: r;\n"
        "begin with q do x := 3; writeln(q.x:0:4) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3.0000\n");
}

TEST(IntToReal, AccumulatesCorrectlyThroughAnArray) {
    auto R = compileAndRun(
        "program p;\n"
        "var a: array[1..3] of real; i: integer; s: real;\n"
        "begin for i := 1 to 3 do a[i] := i;\n"
        " s := 0; for i := 1 to 3 do s := s + a[i]; writeln(s:0:2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6.00\n");
}

// ---------------------------------------------------------------------------
// ISO §6.7.2.2: i mod j lies in [0, j).  C's remainder takes its sign from the
// dividend, so a negative left operand came back negative.
// ---------------------------------------------------------------------------

TEST(Modulo, NegativeDividendYieldsANonNegativeResult) {
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln((-17) mod 5, ' ', 17 mod 5, ' ', (-15) mod 5,\n"
        " ' ', (-1) mod 5) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 2 0 4\n");
}

TEST(Modulo, ConstantFoldingAgreesWithTheRuntime) {
    // A folded constant and a computed value must not disagree.
    auto R = compileAndRun(
        "program p;\n"
        "const c = (-17) mod 5;\n"
        "var a, b: integer;\n"
        "begin a := -17; b := 5; writeln(c, ' ', a mod b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "3 3\n");
}

TEST(Modulo, DivStaysTruncating) {
    // div is unchanged: ISO §6.7.2.2 truncates it toward zero.
    auto R = compileAndRun(
        "program p;\n"
        "begin writeln((-17) div 5, ' ', 17 div 5) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-3 3\n");
}

// ---------------------------------------------------------------------------
// ISO §6.10: 'input' names the same stdin a bare read reaches through getchar.
// Priming the file's lookahead used to consume a character that the bare path
// then never saw, so a program lost its first byte by naming input in the
// header.
// ---------------------------------------------------------------------------

TEST(StandardInput, HeaderParameterDoesNotConsumeTheFirstCharacter) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var i: integer;\n"
        "begin read(i); writeln(i) end.\n", "", "42\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

TEST(StandardInput, HeaderParameterDoesNotConsumeTheFirstChar) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char;\n"
        "begin read(c); writeln(c) end.\n", "", "AB\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "A\n");
}

TEST(StandardInput, TestingEofFirstDoesNotConsumeAnything) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var i: integer;\n"
        "begin writeln(eof); read(i); writeln(i) end.\n", "", "42\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "false\n42\n");
}

TEST(StandardInput, ExplicitAndImplicitReadsShareOnePosition) {
    // read(input, x) goes through the file object and read(x) through the
    // stream directly; they have to agree on where the cursor is.
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var a, b: char;\n"
        "begin read(input, a); read(b); writeln(a, b) end.\n", "", "AB\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "AB\n");
}

TEST(StandardInput, EolnAndReadlnWalkTwoLines) {
    auto R = compileAndRun(
        "program p(input, output);\n"
        "var c: char;\n"
        "begin while not eoln do begin read(c); write(c) end; readln;\n"
        " writeln;\n"
        " while not eoln do begin read(c); write(c) end; writeln end.\n",
        "", "ab\ncd\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ab\ncd\n");
}

// ---------------------------------------------------------------------------
// ISO §6.2.2.3: a type or constant declared in a block is invisible outside it,
// and a variable declared alongside it sees the block's own declaration.
// ---------------------------------------------------------------------------

TEST(BlockScope, LocalTypeShadowsAnOuterTypeOfTheSameName) {
    auto R = compileAndRun(
        "program p;\n"
        "type t = char;\n"
        "var g: t;\n"
        "procedure q; type t = integer; var v: t;\n"
        "begin v := 65; writeln('inner ', v) end;\n"
        "begin g := 'Z'; writeln('outer ', g); q;\n"
        " writeln('outer ', g) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer Z\ninner 65\nouter Z\n");
}

TEST(BlockScope, LocalTypeShadowsInTheOtherDirection) {
    auto R = compileAndRun(
        "program p;\n"
        "type t = integer;\n"
        "var g: t;\n"
        "procedure q; type t = char; var v: t;\n"
        "begin v := 'Q'; writeln('inner ', v) end;\n"
        "begin g := 7; writeln('outer ', g); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 7\ninner Q\n");
}

TEST(BlockScope, ALocalTypeDoesNotLeakToASibling) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure a; type t = char; var v: t;\n"
        "begin v := 'X'; writeln('a ', v) end;\n"
        "procedure b; type t = integer; var v: t;\n"
        "begin v := 9; writeln('b ', v) end;\n"
        "begin a; b; a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a X\nb 9\na X\n");
}

TEST(BlockScope, ALocalConstantShadowsAndDoesNotLeak) {
    auto R = compileAndRun(
        "program p;\n"
        "const c = 1;\n"
        "procedure q; const c = 2; begin writeln('inner ', c) end;\n"
        "begin writeln('outer ', c); q; writeln('outer ', c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 1\ninner 2\nouter 1\n");
}

TEST(BlockScope, ANestedProcedureSeesTheEnclosingProceduresType) {
    auto R = compileAndRun(
        "program p;\n"
        "procedure a; type t = integer; var v: t;\n"
        " procedure b; var w: t; begin w := 3; writeln('b ', w) end;\n"
        "begin v := 1; writeln('a ', v); b end;\n"
        "begin a end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "a 1\nb 3\n");
}

TEST(BlockScope, ALocalRecordTypeShadowsAnOuterOne) {
    auto R = compileAndRun(
        "program p;\n"
        "type r = record x: integer end;\n"
        "var g: r;\n"
        "procedure q; type r = record y: char end; var v: r;\n"
        "begin v.y := 'K'; writeln('inner ', v.y) end;\n"
        "begin g.x := 4; writeln('outer ', g.x); q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "outer 4\ninner K\n");
}

// ---------------------------------------------------------------------------
// EP §6.7.5.4: substr(s, i, n) takes n characters starting at i.  The third
// argument is a count, which coincides with an end index only when i is 1.
// ---------------------------------------------------------------------------

TEST(Substr, ThirdArgumentIsALengthNotAnEndIndex) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh';\n"
        " writeln(substr(t,2,3), ' ', substr(t,1,3), ' ', substr(t,3,1),\n"
        " ' ', substr(t,7,2)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bcd abc c gh\n");
}

TEST(Substr, ExtractsAWordFromTheMiddle) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'Hello World'; writeln(substr(t, 7, 5)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "World\n");
}

TEST(Substr, ZeroLengthYieldsTheEmptyString) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abc'; writeln('[', substr(t, 2, 0), ']') end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[]\n");
}

TEST(Substr, ReachingPastTheEndIsAnError) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abc'; writeln(substr(t, 2, 10)) end.\n", kEP13);
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("outside a string of length 3"), std::string::npos)
        << R.Stderr;
}

TEST(Substr, OmittingTheLengthTakesTheRest) {
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh'; writeln(substr(t, 4)) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "defgh\n");
}

TEST(Substr, ASubstringVariableStillUsesBounds) {
    // EP §6.5.6 writes s[i..j] with an end index, so the two notations differ
    // and both have to keep working.
    auto R = compileAndRun(
        "program p;\n"
        "var t: string(20);\n"
        "begin t := 'abcdefgh'; writeln(t[2..4]) end.\n", kEP13);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "bcd\n");
}

// ---------------------------------------------------------------------------
// ISO §6.9.3.1: a field width applies wherever the value is going, including
// a text file.  The formatted writers ignored the file and wrote to stdout.
// ---------------------------------------------------------------------------

TEST(FileWrite, FieldWidthsGoToTheFileNotStdout) {
    auto R = compileAndRun(
        "program p;\n"
        "var f: text; c: char;\n"
        "begin rewrite(f, 'plang_fw_test.txt');\n"
        " write(f, 42:5); write(f, 'x':3); writeln(f, 1.5:8:2);\n"
        " close(f); reset(f, 'plang_fw_test.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // The trailing character is a space, not the newline the writeln put in
    // the file: §6.4.3.5 gives f^ the value of a space at a line marker, so
    // reading a text file character by character never yields one.
    EXPECT_EQ(R.Stdout, "   42  x    1.50 ");
}

// ---------------------------------------------------------------------------
// ISO §6.6.6.4: succ and pred yield a value of the argument's type.
// ---------------------------------------------------------------------------

TEST(SuccPred, KeepsTheCharType) {
    auto R = compileAndRun(
        "program p;\n"
        "var c: char;\n"
        "begin c := succ('a'); writeln(c); c := pred('z'); writeln(c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "b\ny\n");
}

TEST(SuccPred, KeepsTheEnumerationType) {
    auto R = compileAndRun(
        "program p;\n"
        "type col = (red, green, blue);\n"
        "var c: col;\n"
        "begin c := succ(red); writeln(ord(c));\n"
        " c := pred(blue); writeln(ord(c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n1\n");
}

TEST(SuccPred, StaysIntegerForAnIntegerArgument) {
    auto R = compileAndRun(
        "program p;\n"
        "var i: integer;\n"
        "begin i := succ(5); writeln(i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "6\n");
}

TEST(SuccPred, RejectsANonOrdinalArgument) {
    auto R = compileAndRun(
        "program p;\n"
        "var r: real;\n"
        "begin r := succ(1.5); writeln(r) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("requires an ordinal argument"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.7.2.5: pointers compare with = and <> against each other and against
// nil.  Rejecting that made every linked structure inexpressible.
// ---------------------------------------------------------------------------

TEST(PointerCompare, AgainstNil) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var q: pi;\n"
        "begin q := nil; writeln(q = nil, ' ', q <> nil);\n"
        " new(q); writeln(q = nil, ' ', q <> nil); dispose(q) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\nfalse true\n");
}

TEST(PointerCompare, TwoPointersOfTheSameType) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a, b: pi;\n"
        "begin new(a); b := a; writeln(a = b);\n"
        " new(b); writeln(a = b); dispose(a); dispose(b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\nfalse\n");
}

TEST(PointerCompare, WalksALinkedList) {
    auto R = compileAndRun(
        "program p;\n"
        "type pn = ^node; node = record v: integer; next: pn end;\n"
        "var head, q: pn; i: integer;\n"
        "begin head := nil;\n"
        " for i := 3 downto 1 do\n"
        "  begin new(q); q^.v := i; q^.next := head; head := q end;\n"
        " q := head;\n"
        " while q <> nil do begin write(q^.v, ' '); q := q^.next end;\n"
        " writeln end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 3 \n");
}

TEST(PointerCompare, OrderingOperatorsAreRejected) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a, b: pi;\n"
        "begin a := nil; b := nil; writeln(a < b) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("not defined for pointers"), std::string::npos)
        << R.Stderr;
}

TEST(PointerCompare, APointerAndAnIntegerAreStillIncomparable) {
    auto R = compileAndRun(
        "program p;\n"
        "type pi = ^integer;\n"
        "var a: pi; n: integer;\n"
        "begin a := nil; n := 0; writeln(a = n) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot compare"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.9.3.4.1: the floating-point representation of a real
//
// A real written without a fraction length is written with an exponent, in a
// shape printf has no conversion for: the field width sets how many decimal
// places are written rather than how much padding goes in front, and a
// positive value still occupies its sign column so that a column of them lines
// up.  The runtime used C's %g and %e, so `writeln(1.0)` wrote `1`.
// ---------------------------------------------------------------------------

TEST(WriteReal, DefaultIsTheFloatingPointRepresentation) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0); writeln(-2.5); writeln(0.0) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000\n"
                        "-2.50000000000000e+000\n"
                        " 0.00000000000000e+000\n");
}

// DecPlaces := ActWidth - ExpDigits - 5, so a wider field is written to more
// decimal places and fills exactly, rather than being padded with spaces.
TEST(WriteReal, WidthSetsTheNumberOfDecimalPlaces) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0:20); writeln(1.0:12); writeln(1.0:9) end.\n",
        "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.000000000000e+000\n"
                        " 1.0000e+000\n"
                        " 1.0e+000\n");
}

// A width too small for the representation is raised to the smallest it fits
// in, which is ExpDigits + 6.
TEST(WriteReal, TooNarrowAWidthIsRaisedToTheMinimum) {
    auto R = compileAndRun(
        "program p(output); begin writeln(1.0:1) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.0e+000\n");
}

// Three exponent digits cover the whole range a double reaches, so no value is
// written in a shape other than the advertised one.
TEST(WriteReal, TheExponentIsAlwaysThreeDigits) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(1.0e308); writeln(1.0e-10) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+308\n"
                        " 1.00000000000000e-010\n");
}

// §6.9.3.4.2: naming a fraction length asks for the fixed-point form instead,
// which is the one form that was already right.
TEST(WriteReal, AFractionLengthGivesTheFixedPointForm) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln(3.14159:8:3); writeln(2.5:1:1) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "   3.142\n2.5\n");
}

// The sign character is '-' only when the value is negative and has not
// rounded away, so a small negative written to few places is not '-0'.
TEST(WriteReal, ANegativeRoundingToZeroKeepsTheSpace) {
    auto R = compileAndRun(
        "program p(output); begin writeln(-1.0e-30:9) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-1.0e-030\n");
}

// The same representation on a text file as on output.
TEST(WriteReal, ATextFileGetsTheSameRepresentation) {
    auto R = compileAndRun(
        "program p(output); var f: text; c: char;\n"
        "begin rewrite(f, 'r.txt'); writeln(f, 1.0); close(f);\n"
        " reset(f, 'r.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n", "-std=iso7185");
    // The trailing space is the line marker, which §6.4.3.5 reads as one.
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000 ");
}

// EP §6.7.5.5: writestr is defined in terms of writing to a text file, so it
// gets the representation too.
TEST(WriteReal, WritestrGetsTheSameRepresentation) {
    auto R = compileAndRun(
        "program p(output); var s: string(40);\n"
        "begin writestr(s, 1.0); writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1.00000000000000e+000\n");
}

// A real written out and read back is the value that was written.
TEST(WriteReal, TheRepresentationReadsBack) {
    auto R = compileAndRun(
        "program p(output); var f: text; x: real;\n"
        "begin rewrite(f, 'rt.txt'); writeln(f, 1.0/3.0); close(f);\n"
        " reset(f, 'rt.txt'); read(f, x); close(f);\n"
        " writeln(x:20:15) end.\n", "-std=iso7185");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "   0.333333333333333\n");
}

// ---------------------------------------------------------------------------
// EP §6.9.3.6: writing a complex value
// ---------------------------------------------------------------------------

TEST(WriteComplex, WritesAsAParenthesisedPair) {
    auto R = compileAndRun(
        "program p(output); var a: complex;\n"
        "begin a := cmplx(3.0, 4.0); writeln(a) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    // Each half is a real and is written as one, in the representation of
    // ISO §6.9.3.4.1 and at the default width for a real.
    EXPECT_EQ(R.Stdout, "( 3.00000000000000e+000, 4.00000000000000e+000)\n");
}

TEST(WriteComplex, HonoursWidthAndFractionDigits) {
    auto R = compileAndRun(
        "program p(output); var a: complex;\n"
        "begin a := cmplx(3.0, -4.5); writeln(a:8:2) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "(    3.00,   -4.50)\n");
}

TEST(WriteComplex, GoesToATextFile) {
    auto R = compileAndRun(
        "program p(output); var f: text; z: complex; c: char;\n"
        "begin z := cmplx(1.5, 2.5);\n"
        " rewrite(f, 'cplx.txt'); writeln(f, z); close(f);\n"
        " reset(f, 'cplx.txt');\n"
        " while not eof(f) do begin read(f, c); write(c) end;\n"
        " close(f) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "( 1.50000000000000e+000, 2.50000000000000e+000) ");
}

TEST(WriteComplex, AnExpressionResultIsWritable) {
    auto R = compileAndRun(
        "program p(output); var a, b: complex;\n"
        "begin a := cmplx(1.0, 2.0); b := cmplx(3.0, 4.0);\n"
        " writeln(a + b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "( 4.00000000000000e+000, 6.00000000000000e+000)\n");
}

// ---------------------------------------------------------------------------
// ISO §6.9.3: only a write-parameter may be written
// ---------------------------------------------------------------------------

TEST(WriteTypeCheck, ARecordIsRejected) {
    auto R = compileAndRun(
        "program p(output); type r = record x: integer end;\n"
        "var q: r; begin writeln(q) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

TEST(WriteTypeCheck, AnArrayIsRejected) {
    auto R = compileAndRun(
        "program p(output); var a: array[1..3] of integer;\n"
        "begin writeln(a) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.2.3: an enumeration written inline still introduces its constants
// ---------------------------------------------------------------------------

TEST(InlineEnum, AVarDeclarationIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output); var e: (red, green, blue);\n"
        "begin e := blue; writeln(ord(e)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(InlineEnum, ALocalVarDeclarationIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure q; var e: (x, y); begin e := y; writeln(ord(e)) end;\n"
        "begin q end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

TEST(InlineEnum, ARecordFieldIntroducesItsConstants) {
    auto R = compileAndRun(
        "program p(output); var r: record c: (red, green) end;\n"
        "begin r.c := green; writeln(ord(r.c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1\n");
}

// ---------------------------------------------------------------------------
// EP §6.4.3.3 / §6.7.3: a string is a valid function result type
// ---------------------------------------------------------------------------

TEST(StringFunction, ResultAssignedToAVariable) {
    auto R = compileAndRun(
        "program p(output); var n: string(20);\n"
        "function f: string(20); begin f := 'abc' end;\n"
        "begin n := f; writeln('[', n, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(StringFunction, ResultWrittenDirectly) {
    // A parameterless function named in an expression is a call, and used to
    // be emitted as a reference to a global named after it.
    auto R = compileAndRun(
        "program p(output); function f: string(20); begin f := 'abc' end;\n"
        "begin writeln('[', f, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(StringFunction, TakesAndReturnsAString) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function g(s: string(10)): string(20); begin g := s + '!' end;\n"
        "begin writeln('[', g('hi'), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi!]\n");
}

TEST(StringFunction, ResultParticipatesInConcatenation) {
    auto R = compileAndRun(
        "program p(output); var n: string(30);\n"
        "function f: string(20); begin f := 'abc' end;\n"
        "begin n := f + 'def'; writeln('[', n, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abcdef]\n");
}

TEST(StringFunction, ResultParticipatesInComparison) {
    auto R = compileAndRun(
        "program p(output); function f: string(20); begin f := 'abc' end;\n"
        "begin writeln(f = 'abc', ' ', length(f)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true 3\n");
}

TEST(StringFunction, RecursesThroughTheEmptyString) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function rep(n: integer): string(20);\n"
        "begin if n <= 0 then rep := '' else rep := 'x' + rep(n - 1) end;\n"
        "begin writeln('[', rep(3), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xxx]\n");
}

// ---------------------------------------------------------------------------
// A string component is written and read through its own address
// ---------------------------------------------------------------------------

TEST(StringComponent, ARecordFieldRoundTrips) {
    auto R = compileAndRun(
        "program p(output); var r: record s: string(10) end; n: string(20);\n"
        "begin r.s := 'hi'; n := r.s + '!';\n"
        " writeln('[', r.s, '][', n, ']', ' ', r.s = 'hi') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi][hi!] true\n");
}

TEST(StringComponent, AnArrayElementRoundTrips) {
    auto R = compileAndRun(
        "program p(output); var a: array[1..2] of string(10);\n"
        "begin a[1] := 'one'; a[2] := 'two';\n"
        " writeln('[', a[1], '][', a[2], ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[one][two]\n");
}

TEST(StringComponent, APointerTargetRoundTrips) {
    auto R = compileAndRun(
        "program p(output); type ps = ^string(10); var q: ps;\n"
        "begin new(q); q^ := 'hi'; writeln('[', q^, ']'); dispose(q) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi]\n");
}

// ---------------------------------------------------------------------------
// EP §6.1.8: the zero-length string
// ---------------------------------------------------------------------------

TEST(EmptyString, IsAssignableAndHasLengthZero) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := ''; writeln('[', s, '] ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[] 0\n");
}

TEST(EmptyString, ConcatenatesAndCompares) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := '' + 'ab'; writeln('[', s, '] ', s = 'ab', ' ', '' = '') end.\n",
        kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab] true true\n");
}

TEST(EmptyString, IsStillRejectedUnderIso7185) {
    auto R = compileAndRun(
        "program p(output); begin writeln('') end.\n", "-std=iso7185");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("at least one character"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.8.3.2: a char is a string-compatible operand of '+'
// ---------------------------------------------------------------------------

TEST(CharConcat, CharPlusString) {
    auto R = compileAndRun(
        "program p(output); var c: char; s: string(10);\n"
        "begin c := 'x'; s := 'ab'; s := c + s; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xab]\n");
}

TEST(CharConcat, StringPlusChar) {
    auto R = compileAndRun(
        "program p(output); var c: char; s: string(10);\n"
        "begin c := 'x'; s := 'ab'; s := s + c; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abx]\n");
}

TEST(CharConcat, CharPlusChar) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); c: char;\n"
        "begin c := 'c'; s := 'a' + 'b' + c; writeln('[', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[abc]\n");
}

TEST(CharConcat, CharPlusCharIsStillNotArithmeticUnderIso7185) {
    auto R = compileAndRun(
        "program p(output); var c: char; begin c := 'a' + 'b' end.\n",
        "-std=iso7185");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("requires numeric operands"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.7.3.1: the undiscriminated 'string' parameter-form
// ---------------------------------------------------------------------------

TEST(UndiscriminatedString, AcceptsALiteral) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure q(s: string); begin writeln('[', s, ']') end;\n"
        "begin q('xy') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[xy]\n");
}

TEST(UndiscriminatedString, AcceptsAnyCapacity) {
    auto R = compileAndRun(
        "program p(output); var a: string(5); b: string(40);\n"
        "procedure q(s: string); begin writeln('[', s, '] ', length(s)) end;\n"
        "begin a := 'ab'; b := 'cdefgh'; q(a); q(b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab] 2\n[cdefgh] 6\n");
}

TEST(UndiscriminatedString, IsPassedByValue) {
    auto R = compileAndRun(
        "program p(output); var a: string(5);\n"
        "procedure q(s: string); begin s := 'zz'; writeln('[', s, ']') end;\n"
        "begin a := 'ab'; q(a); writeln('[', a, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[zz]\n[ab]\n");
}

TEST(UndiscriminatedString, WorksAsAFunctionParameter) {
    auto R = compileAndRun(
        "program p(output);\n"
        "function f(s: string): string(20); begin f := s + '!' end;\n"
        "begin writeln('[', f('hi'), ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hi!]\n");
}

TEST(UndiscriminatedString, PassesThroughAProceduralParameter) {
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure shows(s: string); begin writeln('[', s, ']') end;\n"
        "procedure runs(procedure s(t: string)); begin s('hello') end;\n"
        "begin runs(shows) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[hello]\n");
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
// EP §6.8.3.2: 'pow' yields the type of its base
// ---------------------------------------------------------------------------

TEST(IntegerPow, AnIntegerBaseYieldsAnInteger) {
    auto R = compileAndRun(
        "program p(output); var i: integer;\n"
        "begin i := 2 pow 10; writeln(i) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1024\n");
}

TEST(IntegerPow, IsExactBeyondTheRangeOfADouble) {
    // Routed through std::pow this comes back rounded.
    auto R = compileAndRun(
        "program p(output); begin writeln(3 pow 39) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4052555153018976267\n");
}

TEST(IntegerPow, HandlesNegativeBasesAndZeroExponent) {
    auto R = compileAndRun(
        "program p(output);\n"
        "begin writeln((-3) pow 3, ' ', (-3) pow 2, ' ', 5 pow 0) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "-27 9 1\n");
}

TEST(IntegerPow, ARealBaseStillYieldsAReal) {
    auto R = compileAndRun(
        "program p(output); var r: real;\n"
        "begin r := 2.0 pow 3; writeln(r:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8.0\n");
}

TEST(IntegerPow, DoubleStarAlwaysYieldsAReal) {
    auto R = compileAndRun(
        "program p(output); var r: real;\n"
        "begin r := 2 ** 3; writeln(r:0:1) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8.0\n");
}

TEST(IntegerPow, FoldsInAConstantExpressionAndAnArrayBound) {
    auto R = compileAndRun(
        "program p(output); const k = 2 pow 3;\n"
        "var a: array[1..k] of integer;\n"
        "begin a[8] := 5; writeln(k, ' ', a[8]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "8 5\n");
}

TEST(IntegerPow, ANegativeExponentIsReportedAtRuntime) {
    auto R = compileAndRun(
        "program p(output); var i, j: integer;\n"
        "begin j := -3; i := 2 pow j; writeln(i) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("negative exponent"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.9.2.2: a string value shall fit the capacity it is assigned to
// ---------------------------------------------------------------------------

TEST(StringCapacity, AnOverLongLiteralIsRejectedAtCompileTime) {
    auto R = compileAndRun(
        "program p(output); var s: string(3);\n"
        "begin s := 'abcdef' end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not fit a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverLongValueInitializerIsRejected) {
    auto R = compileAndRun(
        "program p(output); var s: string(3) value 'abcdef';\n"
        "begin writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("does not fit a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverLongValueIsReportedAtRuntime) {
    auto R = compileAndRun(
        "program p(output); var s: string(3); u: string(10);\n"
        "begin u := 'abcdef'; s := u; writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("assigned to a string(3)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AnOverflowingConcatenationIsReported) {
    auto R = compileAndRun(
        "program p(output); var s: string(4); a, b: string(4);\n"
        "begin a := 'abc'; b := 'def'; s := a + b end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("assigned to a string(4)"), std::string::npos)
        << R.Stderr;
}

TEST(StringCapacity, AValueThatFitsIsUnaffected) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); u: string(3);\n"
        "begin u := 'ab'; s := u; s := 'abcdefghij';\n"
        " writeln('[', u, '][', s, ']') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "[ab][abcdefghij]\n");
}

// ---------------------------------------------------------------------------
// EP §6.5.3.2: a string has char components selectable by index
// ---------------------------------------------------------------------------

TEST(StringIndex, SelectsACharacter) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'hello'; writeln(s[1], s[5]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "ho\n");
}

TEST(StringIndex, DrivesACharacterLoop) {
    auto R = compileAndRun(
        "program p(output); var s: string(10); i: integer;\n"
        "begin s := 'hello';\n"
        " for i := 1 to length(s) do write(s[i], '-'); writeln end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "h-e-l-l-o-\n");
}

TEST(StringIndex, IsAssignable) {
    auto R = compileAndRun(
        "program p(output); var s: string(20); i, n: integer; c: char;\n"
        "begin s := 'abcdef'; n := length(s);\n"
        " for i := 1 to n div 2 do\n"
        "  begin c := s[i]; s[i] := s[n-i+1]; s[n-i+1] := c end;\n"
        " writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "fedcba\n");
}

TEST(StringIndex, RunsToTheLengthNotTheCapacity) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'ab'; writeln(s[5]) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("string index 5 out of bounds 1..2"),
              std::string::npos) << R.Stderr;
}

TEST(StringIndex, StartsAtOne) {
    auto R = compileAndRun(
        "program p(output); var s: string(10);\n"
        "begin s := 'ab'; writeln(s[0]) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("string index 0"), std::string::npos) << R.Stderr;
}

TEST(StringIndex, WorksOnAComponentString) {
    auto R = compileAndRun(
        "program p(output); var r: record s: string(10) end;\n"
        "begin r.s := 'hey'; writeln(r.s[2]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "e\n");
}

TEST(StringIndex, DoesNotDisturbSubstringSyntax) {
    auto R = compileAndRun(
        "program p(output); var s: string(20);\n"
        "begin s := 'Pascal'; writeln(s[2..4]) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "asc\n");
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
// ISO §6.1.5: a real literal may carry a scale factor
// ---------------------------------------------------------------------------

TEST(ScaleFactor, IsAcceptedInEveryFormAndValue) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const k = 1e3;\n"
        "var r: real;\n"
        "begin\n"
        "  r := 1e3;     writeln(r:0:2);\n"
        "  r := 1.5E-2;  writeln(r:0:4);\n"
        "  r := 2.5e+10; writeln(r:0:1);\n"
        "  r := k;       writeln(r:0:2)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1000.00\n0.0150\n25000000000.0\n1000.00\n");
}

TEST(ScaleFactor, OneTooLargeToRepresentIsReported) {
    auto R = compileAndEmitIR("program p(output);\nbegin write(1.0e100000) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("out of range"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.2: array index types and the multi-dimension abbreviation
// ---------------------------------------------------------------------------

TEST(MultiDimArray, TheCommaFormIsTheNestedForm) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array[1..2, 1..3] of integer; i, j: integer;\n"
        "begin\n"
        "  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;\n"
        "  writeln(a[1, 1], ' ', a[2, 3], ' ', a[2][3])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 23 23\n");
}

TEST(MultiDimArray, ThreeDimensions) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var t: array[1..2, 1..2, 1..2] of integer;\n"
        "begin t[1, 2, 1] := 42; writeln(t[1, 2, 1]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "42\n");
}

// Only the first dimension indexes a name; the rest index an expression, and
// that path had no extent to test against, so an inner index ran off the end
// in silence.
TEST(MultiDimArray, AnInnerIndexIsBoundsCheckedToo) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var a: array[1..2, 1..3] of integer; i: integer;\n"
        "begin i := 5; a[1, i] := 1 end.\n");
    EXPECT_EQ(R.ExitCode, 70);
    EXPECT_NE(R.Stderr.find("array index 5 out of bounds 1..3"), std::string::npos)
        << R.Stderr;
}

TEST(ArrayIndexType, ANamedEnumerationSpansTheWholeType) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type color = (red, green, blue);\n"
        "var a: array[color] of integer; c: color;\n"
        "begin\n"
        "  for c := red to blue do a[c] := ord(c) + 1;\n"
        "  writeln(a[red], ' ', a[green], ' ', a[blue])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 3\n");
}

TEST(ArrayIndexType, BooleanAndANamedSubrangeAndAnInlineEnumeration) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type idx = 1..3;\n"
        "var b: array[boolean] of integer;\n"
        "    n: array[idx] of integer;\n"
        "    e: array[(lo, mid, hi)] of integer;\n"
        "begin\n"
        "  b[false] := 4; b[true] := 5;\n"
        "  n[1] := 11; n[3] := 33;\n"
        "  e[lo] := 1; e[hi] := 3;\n"
        "  writeln(b[false], b[true], ' ', n[1], n[3], ' ', e[lo], e[hi])\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "45 1133 13\n");
}

TEST(ArrayIndexType, ATypeWithNoBoundedRangeIsRejected) {
    for (const char* T : {"integer", "real"}) {
        auto R = compileAndEmitIR(
            std::string("program p;\nvar a: array[") + T +
            "] of integer;\nbegin end.\n");
        EXPECT_FALSE(R.Ok) << T;
        EXPECT_NE(R.Stderr.find("cannot be an array index type"),
                  std::string::npos) << T << ": " << R.Stderr;
    }
}

// The bounds of an array index are constant expressions, which a subrange
// type-denoter's are not; parsing the index as a type must not narrow them.
TEST(ArrayIndexType, ARangeBoundIsStillAFullConstantExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const k = 4;\n"
        "var a: array[1..k * 2] of integer;\n"
        "begin a[8] := 64; writeln(a[8]) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "64\n");
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.2: packed array[1..n] of char is a string type
// ---------------------------------------------------------------------------

TEST(CharStringType, TakesALiteralAndIsWrittenAndCompared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..5] of char; i: integer;\n"
        "begin\n"
        "  s := 'hello'; t := 'world';\n"
        "  write(s); write(' '); writeln(t);\n"
        "  if s = 'hello' then writeln('eq');\n"
        "  if s < t then writeln('lt');\n"
        "  if s <> t then writeln('ne');\n"
        "  for i := 1 to 5 do write(s[i]);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello world\neq\nlt\nne\nhello\n");
}

// Two equal strings held in different variables used to compare unequal: as
// arrays they fell through to a comparison of their addresses.
TEST(CharStringType, TwoVariablesHoldingTheSameTextAreEqual) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..3] of char;\n"
        "begin s := 'abc'; t := 'abc'; writeln(s = t, ' ', s < t) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\n");
}

TEST(CharStringType, CopiesWholeAndHonoursAFieldWidth) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s, t: packed array[1..5] of char;\n"
        "begin s := 'hello'; t := s;\n"
        "  writeln(t); writeln(t:8); writeln('[', t, ']') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello\n   hello\n[hello]\n");
}

// There is no length field to record a shorter value in, so ISO requires the
// literal to be exactly n characters — too short is as wrong as too long.
TEST(CharStringType, ALiteralOfTheWrongLengthIsRejected) {
    auto R = compileAndEmitIR(
        "program p;\n"
        "var s: packed array[1..5] of char;\n"
        "begin s := 'hi'; s := 'toolong' end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("string of length 2 does not fit"), std::string::npos)
        << R.Stderr;
    EXPECT_NE(R.Stderr.find("string of length 7 does not fit"), std::string::npos)
        << R.Stderr;
}

// The standard makes 'packed' part of what a string type is, and a
// one-character literal is a char rather than a string.
TEST(CharStringType, AnUnpackedCharArrayIsNotAStringType) {
    auto R = compileAndEmitIR(
        "program p(output);\n"
        "var s: array[1..5] of char;\n"
        "begin s := 'hello'; writeln(s) end.\n");
    EXPECT_FALSE(R.Ok);
    EXPECT_NE(R.Stderr.find("cannot be written"), std::string::npos) << R.Stderr;
}

// ---------------------------------------------------------------------------
// ISO §6.4.3.3: variant records
// ---------------------------------------------------------------------------

// The variant part was dropped when the LLVM struct was built, so the tag and
// every variant field were missing from the record's storage and touching one
// was an internal error: "record has no field named 'kind'".
TEST(VariantRecord, TheTagAndEveryVariantFieldAreReachable) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type shape = (circle, rect);\n"
        "     fig = record\n"
        "       name: char;\n"
        "       case kind: shape of\n"
        "         circle: (r: integer);\n"
        "         rect:   (w, h: integer)\n"
        "     end;\n"
        "var f: fig;\n"
        "begin\n"
        "  f.name := 'c'; f.kind := circle; f.r := 5;\n"
        "  writeln(f.name, ' ', ord(f.kind), ' ', f.r);\n"
        "  f.kind := rect; f.w := 2; f.h := 3;\n"
        "  writeln(ord(f.kind), ' ', f.w, ' ', f.h)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "c 0 5\n1 2 3\n");
}

// At most one alternative is active at a time, so they share their storage:
// the first field of each starts at the same place.
TEST(VariantRecord, TheAlternativesShareTheirStorage) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type fig = record\n"
        "       case kind: integer of\n"
        "         1: (r: integer);\n"
        "         2: (w, h: integer)\n"
        "     end;\n"
        "var f: fig;\n"
        "begin f.w := 2; f.h := 3; writeln(f.r) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "2\n");
}

TEST(VariantRecord, AnAnonymousTagStillSelects) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type u = record n: integer;\n"
        "       case boolean of true: (i: integer); false: (c: char) end;\n"
        "var x: u;\n"
        "begin x.n := 1; x.i := 65; writeln(x.n, ' ', x.c) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 A\n");
}

TEST(VariantRecord, ANestedVariantIsLaidOutAfterTheFieldsAroundIt) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record\n"
        "       case a: integer of\n"
        "         1: (x: integer;\n"
        "             case b: integer of\n"
        "               10: (p: integer);\n"
        "               11: (q: real));\n"
        "         2: (y: real)\n"
        "     end;\n"
        "var v: t;\n"
        "begin\n"
        "  v.a := 1; v.x := 7; v.b := 10; v.p := 99;\n"
        "  writeln(v.a, ' ', v.x, ' ', v.b, ' ', v.p);\n"
        "  v.q := 1.5; writeln(v.q:0:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 7 10 99\n1.5\n");
}

TEST(VariantRecord, EmptyAlternativesReserveNothing) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case b: boolean of true: (); false: () end;\n"
        "var z: t;\n"
        "begin z.k := 4; z.b := true; writeln(z.k, ' ', z.b) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "4 true\n");
}

// A record variable, a record reached through a pointer and a record inside an
// array were laid out by different paths, only one of which saw the variant.
TEST(VariantRecord, IsLaidOutTheSameWhereverItIsReached) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type w = record case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "     pw = ^w;\n"
        "var arr: array[1..3] of w; q: pw; i: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do begin arr[i].s := 1; arr[i].a := i * i end;\n"
        "  writeln(arr[1].a, ' ', arr[3].a);\n"
        "  new(q); q^.s := 2; q^.b := 3.25; writeln(q^.b:0:2); dispose(q)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 9\n3.25\n");
}

TEST(VariantRecord, WholeRecordAssignmentCarriesTheVariantPart) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "var x, y: t;\n"
        "begin x.k := 1; x.s := 2; x.b := 2.5; y := x;\n"
        "  writeln(y.k, ' ', y.s, ' ', y.b:0:2) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "1 2 2.50\n");
}

// The constructor built its own name-to-field map from the fixed fields, so a
// tag or variant field named in one was accepted and then dropped.
TEST(VariantRecord, AConstructorFillsTheTagAndVariantFields) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record k: integer;\n"
        "       case s: integer of 1: (a: integer); 2: (b: real) end;\n"
        "var x: t;\n"
        "begin x := t[k: 7; s: 1; a: 9];\n"
        "  writeln(x.k, ' ', x.s, ' ', x.a) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7 1 9\n");
}

TEST(VariantRecord, AnEnumerationDeclaredInsideOneIsInScope) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type t = record case k: (alpha, beta) of\n"
        "       alpha: (c: (red, green));\n"
        "       beta:  (n: integer) end;\n"
        "var v: t;\n"
        "begin v.k := alpha; v.c := green;\n"
        "  writeln(ord(v.k), ' ', ord(v.c)) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 1\n");
}

TEST(CharStringType, GoesWhereAnEPStringIsExpected) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: packed array[1..5] of char; v: string(20);\n"
        "begin s := 'hello'; v := s;\n"
        "  writeln(v, '!'); writeln(s = 'hello') end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "hello!\ntrue\n");
}

// ISO §6.8.2.3: a procedure-statement names a procedure.  A required function
// written as one used to reach codegen, which called a runtime routine that
// does not exist, or one with the wrong arguments and no diagnostic at all.
TEST(RequiredFunction, IsNoStatement) {
    for (const char* Call : {"abs(-1)", "sqr(2)", "trim(s)", "eof"}) {
        auto R = compileAndRun(
            std::string("program p(output);\n"
                        "var s: string(9);\n"
                        "begin s := 'ab'; ") + Call + "; writeln(1) end.\n",
            kEP);
        EXPECT_NE(R.ExitCode, 0) << Call;
        EXPECT_NE(R.Stderr.find("use it in an expression"), std::string::npos)
            << Call << ": " << R.Stderr;
    }
}

// ---------------------------------------------------------------------------
// EP §6.5.6 Substring-variables
// ---------------------------------------------------------------------------

TEST(Substring, IsAVariableAndTakesAnAssignment) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10);\n"
        "begin s := 'abcdef'; s[2..3] := 'XY';\n"
        "  writeln(s, ' ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "aXYdef 6\n");
}

TEST(Substring, LeavesTheRestOfTheStringAlone) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10); i: integer;\n"
        "begin s := 'abcdef';\n"
        "  for i := 1 to 3 do s[i..i] := 'z';\n"
        "  writeln(s, ' ', length(s)) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "zzzdef 6\n");
}

TEST(Substring, TakesTheValueOfAnyStringExpression) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10); t: string(3);\n"
        "begin s := 'abcdef'; t := 'pq';\n"
        "  s[4..6] := t + 'r';\n"
        "  writeln(s) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "abcpqr\n");
}

// EP §6.5.6: the substring is a fixed string of exactly j-i+1 characters, so
// a value of any other length has nowhere to go.
TEST(Substring, TurnsAwayAValueOfAnotherLength) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: string(10);\n"
        "begin s := 'abcdef'; s[2..3] := 'TOOLONG'; writeln(s) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("substring of length 2"), std::string::npos)
        << R.Stderr;
}

TEST(Substring, IsNotStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var s: packed array[1..6] of char;\n"
        "begin s := 'abcdef'; s[2..3] := 'XY'; writeln(s) end.\n");
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("substring operator"), std::string::npos)
        << R.Stderr;
}

// ---------------------------------------------------------------------------
// EP §6.4.2.5 Restricted types
// ---------------------------------------------------------------------------

// The example of §6.4.2.5, whose point is that the interface hides
// real_widget and so leaves an importer nothing it can do with a widget but
// hand it back to the module.
TEST(EP4Restricted, CarriesTheStandardsWidgetExample) {
    auto R = compileAndRun(
        "module widget_module interface;\n"
        "export widgets = (widget, copy_widget, increment_widget, print_widget);\n"
        "type real_widget = record f1: integer; f2: real end;\n"
        "     widget = restricted real_widget;\n"
        "procedure copy_widget(source: real_widget; var target: real_widget);\n"
        "function increment_widget(w: real_widget): widget;\n"
        "procedure print_widget(var f: text; w: real_widget);\n"
        "end;\n"
        "function increment_widget;\n"
        "var mycopy: real_widget;\n"
        "begin mycopy.f1 := w.f1 + 1; mycopy.f2 := w.f2 + 1.0;\n"
        "  increment_widget := mycopy end;\n"
        "procedure copy_widget;\n"
        "begin target := source end;\n"
        "procedure print_widget;\n"
        "begin writeln(f, w.f1, ' ', w.f2:3:1) end;\n"
        "end.\n"
        "program p(output);\n"
        "import widget_module;\n"
        "var a, b: widget;\n"
        "begin copy_widget(a, b); print_widget(output, b) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "0 0.0\n");
}

// EP §6.7.3.3: a var parameter of the underlying type accepts a variable of
// the restricted type, which is how one is ever given a value.
TEST(EP4Restricted, IsFilledThroughAVarParameterOfItsUnderlyingType) {
    auto R = compileAndRun(
        "module m interface;\n"
        "export m = (handle, seth, showh);\n"
        "type rep = record n: integer end;\n"
        "     handle = restricted rep;\n"
        "procedure seth(var h: rep; v: integer);\n"
        "procedure showh(h: rep);\n"
        "end;\n"
        "procedure seth; begin h.n := v end;\n"
        "procedure showh; begin writeln(h.n) end;\n"
        "end.\n"
        "program p(output);\n"
        "import m;\n"
        "var x: handle;\n"
        "begin seth(x, 7); showh(x) end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << R.Stderr;
    EXPECT_EQ(R.Stdout, "7\n");
}

TEST(EP4Restricted, HasNoComponentsToReachInto) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type rw = record f1: integer end;\n"
        "     w = restricted rw;\n"
        "var a: w;\n"
        "begin writeln(a.f1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("components are not accessible"), std::string::npos)
        << R.Stderr;
}

TEST(EP4Restricted, IsNeitherAssignedNorCompared) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type rw = record f1: integer end;\n"
        "     w = restricted rw;\n"
        "var a, b: w; c: rw;\n"
        "begin a := b; c := a; if a = b then writeln('eq') end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("nothing can be assigned"), std::string::npos)
        << R.Stderr;
    EXPECT_NE(R.Stderr.find("can only be passed as a parameter"),
              std::string::npos) << R.Stderr;
}

TEST(EP4Restricted, IsNoArithmeticAndNoIO) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "var a: k;\n"
        "begin writeln(a); read(a); writeln(a + 1) end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_EQ(diagCount(R.Stderr), 3) << R.Stderr;
}

// EP §6.4.3.6: were it a file component, reading the file back would make a
// value of the type without going through the module that owns it.
TEST(EP4Restricted, IsNoComponentOfAFile) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "var f: file of k;\n"
        "begin end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0);
    EXPECT_NE(R.Stderr.find("component type of a file"), std::string::npos)
        << R.Stderr;
}

TEST(EP4Restricted, IsNotStandardPascal) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type k = restricted integer;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0);
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
// ISO 7185 6.8.1: a goto may name a label of an enclosing block, abandoning
// every activation between the two.
// ---------------------------------------------------------------------------

TEST(NonLocalGoto, LeavesAProcedureForTheProgramBlock) {
    auto R = compileAndRun(
        "program p(output);\n"
        "label 9999;\n"
        "var i: integer;\n"
        "procedure bail;\n"
        "begin goto 9999 end;\n"
        "begin\n"
        "  for i := 1 to 3 do begin\n"
        "    writeln('iter ', i:1);\n"
        "    if i = 2 then bail\n"
        "  end;\n"
        "  writeln('not reached');\n"
        "9999:\n"
        "  writeln('landed')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "iter 1\niter 2\nlanded\n");
}

TEST(NonLocalGoto, LeavesEveryActivationBetweenItAndTheLabel) {
    // The goto is two procedures down; both are abandoned.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 1;\n"
        "procedure outer;\n"
        "  procedure inner;\n"
        "  begin writeln('inner'); goto 1 end;\n"
        "begin inner; writeln('not reached in outer') end;\n"
        "begin\n"
        "  outer;\n"
        "  writeln('not reached in main');\n"
        "1:\n"
        "  writeln('landed')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "inner\nlanded\n");
}

TEST(NonLocalGoto, ReturnsToTheActivationTheJumpWasMadeFrom) {
    // Each activation of a recursive owner has a block of its own to return
    // to, so the buffer belongs to the frame and not to the procedure.  The
    // locals of the frame landed in must also survive the jump, which is why
    // k is printed: at -O1 and above it had been kept in a register and came
    // back as 0.
    auto R = compileAndRun(
        "program p(output);\n"
        "procedure search(depth: integer);\n"
        "label 1;\n"
        "var k: integer;\n"
        "  procedure check(d: integer);\n"
        "  begin if d = 2 then goto 1 end;\n"
        "begin\n"
        "  k := depth * 10;\n"
        "  if depth < 3 then search(depth + 1);\n"
        "  check(depth);\n"
        "  writeln('fell through at ', depth:1);\n"
        "1:\n"
        "  writeln('left ', depth:1, ' k=', k:1)\n"
        "end;\n"
        "begin search(1) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "fell through at 3\nleft 3 k=30\n"
              "left 2 k=20\n"
              "fell through at 1\nleft 1 k=10\n");
}

TEST(NonLocalGoto, TheLabelMustBeAtTheOutermostLevelOfItsBlock) {
    // 6.8.1: landing inside a structured statement whose activation was just
    // discarded is what the restriction exists to prevent.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 5;\n"
        "var i: integer;\n"
        "procedure q;\n"
        "begin goto 5 end;\n"
        "begin\n"
        "  for i := 1 to 2 do begin 5: writeln(i) end;\n"
        "  q\n"
        "end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted:\n" << R.Stdout;
    EXPECT_NE(R.Stderr.find("goto '5'"), std::string::npos) << R.Stderr;
}

TEST(NonLocalGoto, AGotoOutOfAWithIsStillALocalOne) {
    // A with pushes a scope, so asking the symbol table whether the label was
    // declared "here" answered no and the goto was read as leaving the block.
    auto R = compileAndRun(
        "program p(output);\n"
        "label 3;\n"
        "type r = record a: integer end;\n"
        "var v: r;\n"
        "begin\n"
        "  v.a := 1;\n"
        "  with v do begin if a = 1 then goto 3; writeln('no') end;\n"
        "3:\n"
        "  writeln('yes')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "yes\n");
}

// ---------------------------------------------------------------------------
// Defects the Pascal Acceptance Test found.
// ---------------------------------------------------------------------------

TEST(ForStatement, TheLimitIsReadBeforeTheControlVariableIsAssigned) {
    // 6.8.3.9 evaluates the initial and final values, and only then assigns.
    // Storing 1 first left the limit expression reading it back.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin\n"
        "  i := 5;\n"
        "  for i := 1 to i do write(i:2);\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1 2 3 4 5\n");
}

TEST(ForStatement, TheLimitIsReadOnlyOnce) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i, n: integer;\n"
        "begin\n"
        "  n := 3;\n"
        "  for i := 1 to n do begin write(i:2); n := 99 end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 1 2 3\n");
}

TEST(Ordinals, SuccAndPredKeepTheArgumentsType) {
    // 6.6.6.4.  The arithmetic is done wide and has to come back, or a
    // boolean result is written as 1 and 0.
    auto R = compileAndRun(
        "program p(output);\n"
        "type color = (red, green, blue);\n"
        "begin\n"
        "  writeln(succ(false), ' ', pred(true));\n"
        "  writeln(succ('a'), pred('z'));\n"
        "  writeln(ord(succ(red)):1, ord(pred(blue)):1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\nby\n11\n");
}

TEST(StringCompare, TwoConstantsCompareAsStrings) {
    // Neither operand is an array type, so the comparison went to the ordinary
    // operators and compared the addresses of the two constants.
    auto R = compileAndRun(
        "program p(output);\n"
        "begin\n"
        "  writeln('farka' <= 'farkz', ' ', 'farkz' <= 'farks');\n"
        "  writeln('abc' < 'abd', ' ', 'abd' > 'abc');\n"
        "  writeln('abc' = 'abc', ' ', 'abc' <> 'abd')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true false\ntrue true\ntrue true\n");
}

TEST(StringConstant, ANamedOneWritesWhatItsLiteralWould) {
    auto R = compileAndRun(
        "program p(output);\n"
        "const s = 'this is a string';\n"
        "begin writeln(s); writeln('this is a string') end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "this is a string\nthis is a string\n");
}

TEST(Parameters, APointerPassedByValueArrivesAsThePointer) {
    // A var parameter and a value parameter of pointer type are both `ptr` in
    // the generated signature; taken for the former, the callee was handed the
    // address of the caller's pointer variable.
    auto R = compileAndRun(
        "program p(output);\n"
        "type iptr = ^integer;\n"
        "var ip: iptr;\n"
        "procedure byval(q: iptr);\n"
        "begin writeln(q^:1); new(q); q^ := 1 end;\n"
        "procedure byref(var q: iptr);\n"
        "begin writeln(q^:1); new(q); q^ := 99 end;\n"
        "begin\n"
        "  new(ip); ip^ := 734;\n"
        "  byval(ip); writeln(ip^:1);\n"
        "  byref(ip); writeln(ip^:1)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "734\n734\n734\n99\n");
}

TEST(Parameters, APackedArrayOfCharIsPassedByValue) {
    // 6.6.3.2: the formal is a variable of its own, so the callee's changes
    // stay with the callee.  An array expression is an address, and the value
    // to hand over has to be read from it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type s10 = packed array [1..10] of char;\n"
        "var s: s10;\n"
        "procedure show(t: s10);\n"
        "begin writeln(t); t[1] := 'X'; writeln(t) end;\n"
        "begin\n"
        "  show('literal   ');\n"
        "  s := 'variable  '; show(s); writeln(s)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "literal   \nXiteral   \nvariable  \nXariable  \nvariable  \n");
}

TEST(FileVariables, AnElementOfAnArrayOfFilesIsAFileVariable) {
    // 6.6.5.2 takes a file-variable, and 6.5 calls an array element one.  Only
    // an identifier was looked for, so rewrite was passed a null pointer.
    auto R = compileAndRun(
        "program p(output);\n"
        "var avf: array [1..3] of text;\n"
        "    i, x: integer;\n"
        "begin\n"
        "  for i := 1 to 3 do begin\n"
        "    rewrite(avf[i]); writeln(avf[i], i + 10) end;\n"
        "  for i := 1 to 3 do begin\n"
        "    reset(avf[i]); readln(avf[i], x); write(x:1, ' ') end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "11 12 13 \n");
}

// ---------------------------------------------------------------------------
// Defects the P5 reference output found, which the acceptance test's own
// annotations did not: the program prints what it should be beside each check,
// and where those two disagreed the annotation was the looser of the pair.
// ---------------------------------------------------------------------------

TEST(FieldWidth, AStringTooLongForItsFieldIsTruncated) {
    // 6.9.3.6: the field is exactly TotalWidth characters, so a longer string
    // loses its tail.  A printf width pads but never truncates.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin for i := 14 downto 1 do writeln('hello, world': i) end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "  hello, world\n hello, world\nhello, world\nhello, worl\n"
              "hello, wor\nhello, wo\nhello, w\nhello, \nhello,\nhello\n"
              "hell\nhel\nhe\nh\n");
}

TEST(FieldWidth, ABooleanIsTruncatedLikeTheStringItIsWrittenAs) {
    // 6.9.3.5 writes a boolean as its char-string would be written.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin\n"
        "  for i := 6 downto 1 do writeln(true: i);\n"
        "  for i := 6 downto 1 do writeln(false: i)\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout,
              "  true\n true\ntrue\ntru\ntr\nt\n"
              " false\nfalse\nfals\nfal\nfa\nf\n");
}

TEST(FieldWidth, AFieldWidthAppliesWhenWritingToAFile) {
    // The width was dropped on the way to the file writer, so the whole string
    // went into a field too small to hold it.
    auto R = compileAndRun(
        "program p(output);\n"
        "type s10 = packed array [1..10] of char;\n"
        "var f: text; s: s10; c: char;\n"
        "begin\n"
        "  s := 'hi there !';\n"
        "  rewrite(f); writeln(f, s:5); writeln(f, s:12);\n"
        "  reset(f);\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then begin readln(f); writeln end\n"
        "    else begin read(f, c); write(c) end\n"
        "  end\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "hi th\n  hi there !\n");
}

TEST(TextFiles, ReadingAtALineMarkerGivesASpace) {
    // 6.4.3.5: the line marker separates lines rather than belonging to one,
    // and f^ is a space wherever one stands.  Reading gave the newline itself.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'how now'); writeln(f, 'brown cow');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then write('<eoln>');\n"
        "    read(f, c); write(c)\n"
        "  end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'how now<eoln> brown cow<eoln> '\n");
}

TEST(TextFiles, TheBufferVariableAtALineMarkerIsASpace) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'ab');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin c := f^; get(f); write(c) end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'ab '\n");
}

TEST(TextFiles, AOneByteBinaryFileKeepsAComponentWorthTen) {
    // The space rule is about text.  On a file of a one-byte type the same
    // byte is a component with the value 10 and has to arrive intact, and
    // component size alone does not tell the two apart.
    auto R = compileAndRun(
        "program p(output);\n"
        "type small = 0..255;\n"
        "var f: file of small; v: small; i: integer;\n"
        "begin\n"
        "  rewrite(f);\n"
        "  for i := 8 to 12 do begin f^ := i; put(f) end;\n"
        "  reset(f);\n"
        "  while not eof(f) do begin v := f^; get(f); write(v:1, ' ') end;\n"
        "  writeln\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "8 9 10 11 12 \n");
}

TEST(TextFiles, TheLastLineIsTerminatedEvenWithoutAWriteln) {
    // 6.4.3.5 makes a text file a sequence of lines, each ended by a marker,
    // so a file built with write alone still reads back as complete lines.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text; c: char;\n"
        "begin\n"
        "  rewrite(f); writeln(f, 'too much'); write(f, 'too soon');\n"
        "  reset(f); write('''');\n"
        "  while not eof(f) do begin\n"
        "    if eoln(f) then write('<eoln>');\n"
        "    read(f, c); write(c)\n"
        "  end;\n"
        "  writeln('''')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "'too much<eoln> too soon<eoln> '\n");
}

TEST(TextFiles, EofIsTrueOnAFileBeingWritten) {
    // 6.6.5.2: rewrite(f) leaves eof(f) true, and writing happens at the end
    // of the file, so it stays true for as long as f is being generated.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f: text;\n"
        "begin\n"
        "  rewrite(f); writeln(eof(f));\n"
        "  writeln(f, 'x'); writeln(eof(f));\n"
        "  reset(f); writeln(eof(f))\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "true\ntrue\nfalse\n");
}

// ---------------------------------------------------------------------------
// Requirements neither the rejection suite nor the acceptance test covers: a
// violation of a "shall" that no test program happened to be written around.
// ---------------------------------------------------------------------------

TEST(FileTypes, AFileVariableCannotBeAssigned) {
    // 6.8.2.2: a file names something outside the program that the variable is
    // a window onto, and copying the window has no meaning.
    auto R = compileAndRun(
        "program p(output);\n"
        "var f, g: text;\n"
        "begin f := g end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file type"), std::string::npos) << R.Stderr;
}

TEST(FileTypes, AStructureHoldingAFileCannotBeAssigned) {
    auto R = compileAndRun(
        "program p(output);\n"
        "type holder = record f: text; n: integer end;\n"
        "var a, b: holder;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file"), std::string::npos) << R.Stderr;
}

TEST(FileTypes, AnArrayOfFilesCannotBeAssigned) {
    // An array of files holds files as surely as a record of them does, and
    // the rule was looking only inside records.
    auto R = compileAndRun(
        "program p(output);\n"
        "type fa = array [1..2] of text;\n"
        "var a, b: fa;\n"
        "begin a := b end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("file"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, TheLabelsMustBeDistinct) {
    // 6.8.3.5.  Two arms holding the same value give the statement two
    // meanings, and which one runs would be an accident of code generation.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin case i of 1: writeln(1); 1: writeln(2) end end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one arm"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, ARangeCollidingWithALabelIsCaught) {
    // A range as a case-constant is EP's (§6.9.3.5), and it stands for every
    // value in it, any of which another arm can repeat.
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin case i of 1..5: writeln(1); 3: writeln(2) end end.\n", kEP);
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one arm"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, ADuplicateIsNamedAsItWasWritten) {
    // The ordinal of a char is not what the reader put in the program.
    auto R = compileAndRun(
        "program p(output);\n"
        "var c: char;\n"
        "begin case c of 'a': writeln(1); 'a': writeln(2) end end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("case label 'a'"), std::string::npos) << R.Stderr;
}

TEST(CaseStatement, DistinctLabelsAreStillAccepted) {
    auto R = compileAndRun(
        "program p(output);\n"
        "var i: integer;\n"
        "begin i := 4;\n"
        "  case i of 1..3: writeln('low'); 4..6: writeln('mid') end\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, "mid\n");
}

TEST(FunctionResult, MustBeSimpleOrPointerUnderIso7185) {
    // 6.6.2 admits a simple-type or a pointer-type and nothing else.
    auto R = compileAndRun(
        "program p(output);\n"
        "type arr = array [1..3] of integer;\n"
        "var v: arr;\n"
        "function q: arr; begin q := v end;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("simple or pointer"), std::string::npos) << R.Stderr;
}

TEST(FunctionResult, ExtendedPascalAllowsAStructuredOne) {
    // EP §6.6.2 lifts the restriction to anything a value can be assigned
    // from, which leaves out only the file types.
    auto R = compileAndRun(
        "program p(output);\n"
        "type arr = array [1..3] of integer;\n"
        "var v: arr; i: integer;\n"
        "function q: arr; begin q := v end;\n"
        "begin\n"
        "  v[1] := 7; v[2] := 8; v[3] := 9;\n"
        "  v := q;\n"
        "  for i := 1 to 3 do write(v[i]:2); writeln\n"
        "end.\n", kEP);
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
    EXPECT_EQ(R.Stdout, " 7 8 9\n");
}

TEST(FunctionResult, AFileResultIsRefusedInBothStandards) {
    for (const std::string Std : {std::string("-std=iso7185"), std::string(kEP)}) {
        auto R = compileAndRun(
            "program p(output);\n"
            "function q: text; begin end;\n"
            "begin end.\n", Std);
        EXPECT_NE(R.ExitCode, 0) << "accepted under " << Std;
    }
}

TEST(VariantPart, TheCaseConstantsMustBeDistinct) {
    // 6.4.3.3, for the reason 6.8.3.5 has: the tag value has to name one
    // variant and not two.
    auto R = compileAndRun(
        "program p(output);\n"
        "type r = record case t: integer of\n"
        "            1: (a: integer);\n"
        "            1: (b: char)\n"
        "         end;\n"
        "var v: r;\n"
        "begin end.\n");
    EXPECT_NE(R.ExitCode, 0) << "accepted";
    EXPECT_NE(R.Stderr.find("more than one variant"), std::string::npos) << R.Stderr;
}

TEST(TextOnlyProcedures, WritelnPageAndEolnRefuseANonTextFile) {
    // 6.9.5, 6.9.4 and 6.6.6.5: all three are about lines, and only a text
    // file has any.  readln was checked and the other three were not.
    struct Case { const char* body; const char* want; };
    const Case Cases[] = {
        {"rewrite(f); writeln(f)",              "'writeln'"},
        {"rewrite(f); page(f)",                 "'page'"},
        {"reset(f); if eoln(f) then writeln(1)","'eoln'"},
        {"reset(f); readln(f, x)",              "'readln'"},
    };
    for (const auto& C : Cases) {
        auto R = compileAndRun(
            std::string("program p(output);\n"
                        "var f: file of integer; x: integer;\n"
                        "begin ") + C.body + " end.\n");
        EXPECT_NE(R.ExitCode, 0) << "accepted: " << C.body;
        EXPECT_NE(R.Stderr.find(C.want), std::string::npos)
            << C.body << "\n" << R.Stderr;
    }
}

TEST(TextOnlyProcedures, AFileOfCharIsAText) {
    // 6.4.3.5 makes a file of char a text file, so all four apply to one.
    auto R = compileAndRun(
        "program p(output);\n"
        "var g: file of char;\n"
        "begin\n"
        "  rewrite(g); writeln(g); page(g);\n"
        "  reset(g); if eoln(g) then writeln('eoln')\n"
        "end.\n");
    ASSERT_EQ(R.ExitCode, 0) << "compile/run failed:\n" << R.Stderr;
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

/// Runs an already-built binary with a standard input that stays open and
/// never produces anything, which is what a terminal at a fresh prompt looks
/// like to a program that tries to read.  compileAndRun redirects from
/// /dev/null, where a read returns EOF at once, so a program that blocks on
/// input cannot be told apart from one that does not.
static std::string runWithStdinHeldOpen(const std::string& Bin) {
    const std::string Dir = makeTempDir();
    const std::string Fifo = Dir + "/stdin";
    if (mkfifo(Fifo.c_str(), 0600) != 0) return "mkfifo failed";
    // Holding the fifo open read-write on a spare descriptor means it has a
    // writer for as long as the shell lives, so the reader never sees EOF —
    // and opening it does not block, which it would with no writer at all.
    //
    // A program that does wait has to be killed for the test to report rather
    // than hang, and the watchdog is written out here rather than left to
    // timeout(1), which is GNU coreutils and is not on a Mac.  A killed
    // program reports 137 — 128 and SIGKILL — where timeout reported 124.
    // The watchdog gets its own standard output rather than inheriting this
    // one, because the caller reads until end of file and an inherited
    // descriptor would hold that open for the whole five seconds even once the
    // program under test had finished.
    const std::string Out = runCmd(
        "exec 9<>" + Fifo + "; " + Bin + " <" + Fifo + " 2>/dev/null & "
        "pid=$!; (sleep 5; kill -9 $pid 2>/dev/null) >/dev/null 2>&1 & "
        "guard=$!; wait $pid; rc=$?; kill $guard 2>/dev/null; "
        "echo \"exit:$rc\"; exec 9>&-");
    removeTempDir(Dir);
    return Out;
}

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

/// Run "plang <args>" and return the exit status along with what it printed.
static std::pair<int, std::string> runPlangRc(const std::string &Args) {
    const std::string Cmd = std::string(PLANG_PATH) + " " + Args + " 2>&1";
    FILE *Pipe = popen(Cmd.c_str(), "r");
    if (!Pipe) return {-1, ""};
    std::string Out;
    char Buf[256];
    while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
    const int Status = pclose(Pipe);
    return {WIFEXITED(Status) ? WEXITSTATUS(Status) : -1, Out};
}

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
