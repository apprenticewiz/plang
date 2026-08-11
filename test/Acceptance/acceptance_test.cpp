// The Pascal Acceptance Test: one whole standard program, compiled, run, and
// compared against output that has been checked by hand.
//
// Everything else in test/ asks whether one construct behaves; this asks
// whether a program that uses nearly all of ISO 7185 at once still does.  The
// difference is not academic — adopting this test turned up eleven defects
// that the per-construct tests had no reason to look for, among them a
// for-statement whose limit was read after the control variable was assigned,
// and a value parameter of pointer type that arrived as the address of the
// caller's variable.
//
// The program is from the Pascal-P5 suite (Copyright (C) 2010 S. A. Moore),
// which is in the public domain, as the ISO 7185 rejection tests under
// test/Conformance also are.
//
// On the expected file: it is plang's own output, audited against the "s/b"
// (should be) annotations the program prints beside each of its 743 checks.
// The author's own reference output is not usable in its place, having been
// produced by an implementation with 32-bit integers, single-precision reals
// and capitalized booleans.  Every check either matches its annotation or
// differs only in what ISO 7185 leaves to the implementation — the number of
// exponent digits (§6.9.3.4.1, three here) and the precision of real
// arithmetic — or in the spacing of an annotation the program writes as prose
// rather than in the field width it asked for.  docs/conformance.md records
// those choices.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

std::string readFile(const std::string& Path) {
    std::ifstream In(Path, std::ios::binary);
    std::ostringstream Ss;
    Ss << In.rdbuf();
    return Ss.str();
}

std::string runCmd(const std::string& Cmd) {
    FILE* Pipe = popen(Cmd.c_str(), "r");
    if (!Pipe) return "";
    std::string Out;
    char Buf[4096];
    while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
    pclose(Pipe);
    return Out;
}

/// The first line at which two texts differ, rendered for a failure message.
/// Diffing 1214 lines by eye is what this saves.
std::string firstDifference(const std::string& Got, const std::string& Want) {
    std::istringstream G(Got), W(Want);
    std::string GL, WL;
    for (int Line = 1;; ++Line) {
        const bool GOk = static_cast<bool>(std::getline(G, GL));
        const bool WOk = static_cast<bool>(std::getline(W, WL));
        if (!GOk && !WOk) return "no differing line, but the texts differ";
        if (!GOk) return "output ends at line " + std::to_string(Line)
                       + ", expected |" + WL + "|";
        if (!WOk) return "output runs past the end at line "
                       + std::to_string(Line) + ": |" + GL + "|";
        if (GL != WL)
            return "line " + std::to_string(Line)
                 + "\n     got |" + GL + "|\nexpected |" + WL + "|";
    }
}

} // namespace

TEST(Acceptance, ISO7185PascalAcceptanceTest) {
    const std::string CaseDir = ACCEPTANCE_CASES_DIR;
    const std::string Src     = CaseDir + "/iso7185pat.pas";
    const std::string Want    = readFile(CaseDir + "/iso7185pat.expected");
    ASSERT_FALSE(Want.empty()) << "expected output missing from " << CaseDir;

    // A directory of its own: the program opens files of its own making, and
    // ctest -j has the other cases running alongside.
    char Tmpl[] = "/tmp/plang_pat_XXXXXX";
    const char* Dir = mkdtemp(Tmpl);
    ASSERT_NE(Dir, nullptr);
    const std::string Work(Dir);
    const std::string Bin = Work + "/pat";
    const std::string Err = Work + "/compile.err";

    // The suite is re-run under other codegen settings by setting this; the
    // program's output is the same at every optimization level, which is
    // itself worth holding to.
    const char* Extra = std::getenv("PLANG_TEST_EXTRA_FLAGS");

    const std::string Compile = std::string(PLANG_PATH)
        + " " + (Extra ? Extra : "") + " -std=iso7185 "
        + Src + " -o " + Bin + " 2>" + Err;
    const int Rc = std::system(Compile.c_str());
    EXPECT_EQ(Rc, 0) << "compilation failed:\n" << readFile(Err);
    ASSERT_TRUE(std::filesystem::exists(Bin));

    const std::string RunErr = Work + "/run.err";
    const std::string Got = runCmd("cd " + Work + " && " + Bin
                                   + " < /dev/null 2>" + RunErr);
    EXPECT_EQ(readFile(RunErr), "") << "the program reported a runtime error";

    EXPECT_EQ(Got, Want) << firstDifference(Got, Want);

    // ISO §6.5.1: the program's own files are its own to open; none of them
    // should be left in the directory it ran in.
    std::error_code Ec;
    std::filesystem::remove_all(Work, Ec);
}
