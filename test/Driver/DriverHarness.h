#pragma once

/// DriverHarness.h — running the real compiler from a test
///
/// Everything here starts a child process, which is what separates these
/// suites from the in-process ones.  The scanner, parser and Sema suites build
/// a Scanner and a Sema and look at what comes back; these build a program,
/// link it, run it and look at what it printed.  That is the only way to test
/// the driver, the linker invocation, the runtime library and the exit status,
/// and it is the only way to be sure the optimizer left the answer alone.
///
/// This was written inside driver_test.cpp, which grew to eleven thousand
/// lines and left the next suite -- Turbo Pascal's -- with a choice between
/// adding to it and copying it.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <sys/stat.h>   // mkfifo, for a standard input that stays open
#include <sys/wait.h>   // WIFEXITED, for the exit status of a driver run
#include <unistd.h>
#include <utility>
#include <vector>

/// Pins the message language for every child process a test starts.
///
/// These cases run the real binary, which resolves its own catalog from the
/// environment, and ~180 of the assertions match English message text.
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
inline const int PinnedLocale = [] {
    ::setenv("LC_ALL", "C", /*overwrite=*/1);
    return 0;
}();

/// A directory of one case's own.  The compiler writes a module's interface
/// file beside its source, under the module's own name, so two tests that use
/// the same module name would otherwise overwrite each other's under `-j`.
inline std::string makeTempDir() {
    char Tmpl[] = "/tmp/plang_case_XXXXXX";
    const char* D = mkdtemp(Tmpl);
    return D ? std::string(D) : std::string("/tmp");
}

inline void removeTempDir(const std::string& Dir) {
    if (Dir.rfind("/tmp/plang_case_", 0) != 0) return;
    std::error_code Ec;
    std::filesystem::remove_all(Dir, Ec);
}

/// Write \p Content to \p Path.
inline bool writeFileAt(const std::string& Path, const std::string& Content) {
    FILE* F = std::fopen(Path.c_str(), "wb");
    if (!F) return false;
    std::fwrite(Content.data(), 1, Content.size(), F);
    std::fclose(F);
    return true;
}

inline std::string runCmd(const std::string& Cmd) {
    FILE* Pipe = popen(Cmd.c_str(), "r");
    if (!Pipe) return "";
    std::string Out;
    char Buf[256];
    while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
    pclose(Pipe);
    return Out;
}

/// Run "plang -pc1 <args>" and capture combined stderr output.
inline std::string runPC1(const std::string& Args) {
    return runCmd(std::string(PLANG_PATH) + " -pc1 " + Args + " 2>&1");
}

/// Run "plang <args>" and capture combined stderr output.
inline std::string runPlang(const std::string& Args) {
    return runCmd(std::string(PLANG_PATH) + " " + Args + " 2>&1");
}

/// Run "plang <args>" and return the exit status along with what it printed.
inline std::pair<int, std::string> runPlangRc(const std::string& Args) {
    const std::string Cmd = std::string(PLANG_PATH) + " " + Args + " 2>&1";
    FILE* Pipe = popen(Cmd.c_str(), "r");
    if (!Pipe) return {-1, ""};
    std::string Out;
    char Buf[256];
    while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
    const int Status = pclose(Pipe);
    return {WIFEXITED(Status) ? WEXITSTATUS(Status) : -1, Out};
}

inline int lineCount(const std::string& S) {
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
inline int diagCount(const std::string& S, const std::string& Sev = "error: ") {
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

/// Flag selecting Extended Pascal, for the tests that exercise EP-only syntax.
inline const std::string kEP = "-std=iso10206";

// ---------------------------------------------------------------------------
// A case directory, and named files in it
// ---------------------------------------------------------------------------

/// A directory of named files that goes away when the case does.
///
/// The one-string entry points below name the source themselves, which is
/// everything a single-file program needs.  Three things need more: a program
/// that opens a data file by name, a multi-unit build, and an include
/// directive, where the name in the source and the name on disk have to agree
/// and the test is about them agreeing.  Turbo Pascal's `{$I common.inc}` is
/// the third, and it is why this exists before it is heavily used.
///
/// Everything runs with this directory as the working directory, so a relative
/// name resolves the way it would for someone building by hand.
class CaseDir {
public:
    CaseDir() : Dir(makeTempDir()) {}
    ~CaseDir() { removeTempDir(Dir); }
    CaseDir(const CaseDir&)            = delete;
    CaseDir& operator=(const CaseDir&) = delete;

    const std::string& path() const { return Dir; }

    /// The full path of \p Name inside this directory.
    std::string at(std::string_view Name) const {
        return Dir + "/" + std::string(Name);
    }

    /// Writes \p Content to \p Name and returns its full path.  \p Name may
    /// name a subdirectory, which is created: an include-path test has to be
    /// able to put the file somewhere the compiler will not find by accident.
    std::string write(std::string_view Name, std::string_view Content) const {
        const std::string Path = at(Name);
        if (const auto Slash = Path.rfind('/'); Slash != std::string::npos) {
            std::error_code Ec;
            std::filesystem::create_directories(Path.substr(0, Slash), Ec);
        }
        writeFileAt(Path, std::string(Content));
        return Path;
    }

    /// What is in \p Name now, for a program that was supposed to write one.
    std::string read(std::string_view Name) const {
        std::FILE* F = std::fopen(at(Name).c_str(), "rb");
        if (!F) return "";
        std::string Out;
        char Buf[512];
        while (const size_t N = std::fread(Buf, 1, sizeof(Buf), F)) Out.append(Buf, N);
        std::fclose(F);
        return Out;
    }

    bool exists(std::string_view Name) const {
        std::error_code Ec;
        return std::filesystem::exists(at(Name), Ec);
    }

    /// Compiles \p Source and runs it, from inside this directory.
    ///
    /// \p Source and any file the program opens are named relatively, which is
    /// the point: an absolute path would resolve for a reason having nothing to
    /// do with what the test is asserting.
    RunResult compileAndRunFile(std::string_view Source,
                                const std::string& ExtraFlags = "",
                                const std::string& StdinText  = "") const {
        const std::string Bin = at("a.out");
        RunResult R;

        const char* EnvFlags = std::getenv("PLANG_TEST_EXTRA_FLAGS");
        const int CompileRc = std::system(
            ("cd " + Dir + " && " + PLANG_PATH
             + " " + (EnvFlags ? EnvFlags : "")
             + " " + ExtraFlags
             + " " + std::string(Source)
             + " -o " + Bin + " 2>compile.err").c_str());
        R.Stderr = read("compile.err");
        if (CompileRc != 0) { R.ExitCode = CompileRc; return R; }

        std::string Redirect = " < /dev/null";
        if (!StdinText.empty()) {
            write("stdin.txt", StdinText);
            Redirect = " < " + at("stdin.txt");
        }
        R.Stdout = runCmd("cd " + Dir + " && " + Bin + Redirect
                          + " 2>run.err; echo \"exit:$?\"");
        R.ExitCode = 0;
        if (const auto Pos = R.Stdout.rfind("exit:"); Pos != std::string::npos) {
            R.ExitCode = std::atoi(R.Stdout.c_str() + Pos + 5);
            R.Stdout.erase(Pos);
        }
        R.Stderr += read("run.err");
        return R;
    }

    /// Compiles \p Source to LLVM IR without linking, from inside this
    /// directory.
    IRResult compileFileToIR(std::string_view Source,
                             const std::string& ExtraFlags = "") const {
        const int Rc = std::system(
            ("cd " + Dir + " && " + PLANG_PATH + " -pc1 " + ExtraFlags
             + " -emit-llvm " + std::string(Source)
             + " -o out.ll 2>compile.err").c_str());
        return { Rc == 0, read("out.ll"), read("compile.err") };
    }

    /// Runs the driver in this directory, as a shell in it would, and returns
    /// its exit status with what it printed.
    std::pair<int, std::string> runPlangIn(const std::string& Args) const {
        FILE* Pipe = popen(("cd " + Dir + " && " + PLANG_PATH + " " + Args
                            + " 2>&1").c_str(), "r");
        if (!Pipe) return {-1, ""};
        std::string Out;
        char Buf[256];
        while (std::fgets(Buf, sizeof(Buf), Pipe)) Out += Buf;
        const int Status = pclose(Pipe);
        return {WIFEXITED(Status) ? WEXITSTATUS(Status) : -1, Out};
    }

private:
    std::string Dir;
};

// ---------------------------------------------------------------------------
// The one-string entry points
// ---------------------------------------------------------------------------

/// Compile Pascal source to LLVM IR and return it as a string.
/// Uses -pc1 -emit-llvm so no linker or llc is involved.
inline IRResult compileAndEmitIR(const std::string& Src,
                                 const std::string& ExtraFlags = "") {
    CaseDir C;
    C.write("case.pas", Src);
    return C.compileFileToIR("case.pas", ExtraFlags);
}

/// Returns true if the IR contains all of the given substrings.
inline bool irContainsAll(const std::string& IR,
                          std::initializer_list<const char*> Patterns) {
    for (const char* P : Patterns)
        if (IR.find(P) == std::string::npos) return false;
    return true;
}

/// Returns true if the IR contains none of the given substrings.
inline bool irContainsNone(const std::string& IR,
                           std::initializer_list<const char*> Patterns) {
    for (const char* P : Patterns)
        if (IR.find(P) != std::string::npos) return false;
    return true;
}

/// Compile Pascal source through the full pipeline and run the resulting
/// binary.  Returns what the program printed, on both streams, and its exit
/// status — the ISO runtime checks report on stderr and a test that ignored
/// them could not tell a clean run from an aborted one.
///
/// PLANG_TEST_EXTRA_FLAGS re-runs the whole suite under other codegen
/// settings — chiefly -O1/-O2/-O3, since every expected value here is also the
/// answer the optimizer has to preserve.
inline RunResult compileAndRun(const std::string& Src,
                               const std::string& ExtraFlags = "",
                               const std::string& StdinText  = "") {
    CaseDir C;
    C.write("case.pas", Src);
    return C.compileAndRunFile("case.pas", ExtraFlags, StdinText);
}

/// Compile a module source to object + PMI, then compile a program that
/// imports the module and link them together.  Returns the program's output.
struct TwoFileResult {
    int         ExitCode{-1};
    std::string Stdout;
    std::string Stderr;
};

inline TwoFileResult compileTwoFiles(const std::string& ModSrc,
                                     const std::string& ProgSrc,
                                     const std::string& ExtraFlags = "") {
    CaseDir C;
    C.write("mod.pas", ModSrc);
    C.write("prog.pas", ProgSrc);
    TwoFileResult R;

    // Step 1: compile the module to an object file.  The front end also writes
    // ModuleName.pmi beside the source, under a name this test does not
    // choose, which is why each case gets a directory of its own.
    {
        const int Rc = std::system(("cd " + C.path() + " && " + PLANG_PATH
                                    + " " + ExtraFlags + " -c mod.pas -o mod.o"
                                    + " 2>mod.err").c_str());
        R.Stderr += C.read("mod.err");
        if (Rc != 0) { R.ExitCode = Rc; return R; }
    }

    // Step 2: compile the program and link it with that object.  The driver
    // routes a .o straight to the linker; -I points the importer at the .pmi.
    {
        const int Rc = std::system(("cd " + C.path() + " && " + PLANG_PATH
                                    + " " + ExtraFlags + " -I. prog.pas mod.o"
                                    + " -o prog 2>prog.err").c_str());
        R.Stderr += C.read("prog.err");
        if (Rc != 0) { R.ExitCode = Rc; return R; }
    }

    // Step 3: run it.
    R.Stdout = runCmd("cd " + C.path()
                      + " && ./prog < /dev/null 2>run.err; echo \"exit:$?\"");
    R.ExitCode = 0;
    if (const auto Pos = R.Stdout.rfind("exit:"); Pos != std::string::npos) {
        R.ExitCode = std::atoi(R.Stdout.c_str() + Pos + 5);
        R.Stdout.erase(Pos);
    }
    R.Stderr += C.read("run.err");
    return R;
}

/// Runs an already-built binary with a standard input that stays open and
/// never produces anything, which is what a terminal at a fresh prompt looks
/// like to a program that tries to read.  compileAndRun redirects from
/// /dev/null, where a read returns EOF at once, so a program that blocks on
/// input cannot be told apart from one that does not.
inline std::string runWithStdinHeldOpen(const std::string& Bin) {
    CaseDir C;
    const std::string Fifo = C.at("stdin");
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
    return runCmd(
        "exec 9<>" + Fifo + "; " + Bin + " <" + Fifo + " 2>/dev/null & "
        "pid=$!; (sleep 5; kill -9 $pid 2>/dev/null) >/dev/null 2>&1 & "
        "guard=$!; wait $pid; rc=$?; kill $guard 2>/dev/null; "
        "echo \"exit:$rc\"; exec 9>&-");
}
