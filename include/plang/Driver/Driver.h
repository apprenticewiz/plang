#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SourceManager.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace plang {

/// Controls how far the compilation pipeline runs.
enum class OutputMode {
    Executable, ///< (default) link to a native binary
    Object,     ///< -c        compile to a native object file
    Assembly,   ///< -S        compile to native assembly
    LLVMIr,     ///< -emit-llvm   emit textual LLVM IR
    DumpAst,    ///< -dump-ast    print the typed AST and stop
};

/// Parsed command-line options for the plang driver.
struct Options {
    std::string  inputFile;
    std::string  outputFile;               ///< empty = derive from inputFile / mode
    OutputMode   mode{OutputMode::Executable};
    int          optLevel{0};              ///< 0–3, forwarded to the front end and llc
    bool         verbose{false};           ///< -v: echo each tool invocation
    bool         dryRun{false};            ///< -###: print commands, don't run
    bool         saveTemps{false};         ///< -save-temps: keep .ll and .o files
    bool         debug{false};             ///< -g: generate debug information
    std::string  target;                   ///< --target=<triple>
    std::string  std;                      ///< -std=<dialect>
    bool         suppressWarnings{false};  ///< -w
    bool         warningsAsErrors{false};  ///< -Werror
    bool         rangeChecks{true};        ///< -fno-range-checks disables
    std::vector<std::string> frontendArgs; ///< options passed straight to -pc1
    std::vector<std::string> linkerArgs;   ///< -Wl,… / -Xlinker / -L / -l extras
    std::vector<std::string> modulePaths;  ///< -I<dir> module search paths
    std::vector<std::string> extraInputFiles; ///< additional .pas files for multi-file builds
};

/// Compiler driver.  Parses arguments and orchestrates the compilation
/// pipeline: plang -pc1 (Pascal front end), llc (code generation), and the
/// platform linker — ld.lld on ELF targets, the system ld on macOS.
class Driver {
public:
    /// Construct with \p Argv0 (argv[0] from main) so that the driver can
    /// locate the running binary portably on Linux, macOS, and Windows via
    /// llvm::sys::fs::getMainExecutable.
    explicit Driver(const char *Argv0 = nullptr);

    /// Parse \p argc / \p argv, run the pipeline, and return a Unix exit code.
    int run(int argc, char *argv[]);

    /// Report a driver diagnostic.
    ///
    /// The driver's diagnostics are about the command line or the toolchain, so
    /// none of them has a place in a source file to point at; they print under
    /// the program name instead — "plang: error: no input files".  They are
    /// printed as they are reported rather than collected and flushed at a
    /// phase boundary the way the front end's are, because the driver
    /// interleaves them with the output of the tools it runs.
    ///
    /// Going through the engine rather than straight to stderr is what subjects
    /// them to -w, -Werror, -Wno-<name> and the error limit.
    void diag(DiagID ID, std::initializer_list<std::string_view> Args = {});

    /// Spawn \p Prog with the given argument vector (no shell — uses
    /// llvm::sys::ExecuteAndWait).  Prints each argument when \p Verbose or
    /// \p DryRun; skips execution when DryRun.
    int runTool(const std::string &Prog,
                const std::vector<std::string> &Args,
                bool Verbose, bool DryRun = false);

private:
    std::string       ExePath_; ///< absolute path to the running plang binary
    DiagnosticsEngine Diags_;
    SourceManager     SrcMgr_;  ///< empty; a driver diagnostic has no buffer
    bool              UseColor_{false};

    /// Settle -w, -Werror, -Wno-<name> and -f{,no-}color-diagnostics before the
    /// rest of the command line is parsed, so that a diagnostic raised during
    /// that parse is already subject to them however late they appear on it.
    /// clang settles the same question first, in CreateAndPopulateDiagOpts.
    void        configureDiagnostics(int argc, char *argv[]);

    Options     parseArgs(int argc, char *argv[]);
    int         compile(const Options &opts);
    std::string defaultOutput(const std::string &inputFile, OutputMode mode);

    std::string findInstallDir() const;
    std::string findSelf()       const;
    static void printVersion();
    static void usage();
};

} // namespace plang
