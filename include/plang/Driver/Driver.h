#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/SourceManager.h"

#include <initializer_list>
#include <optional>
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
    DumpTokens,    ///< -dump-tokens      print the token stream and stop
    DumpParseTree, ///< -dump-parse-tree  print the pre-Sema parse tree and stop
    /// -dump-vmt: Turbo Tier 5, Cluster A item 1's own debug-introspection
    /// mode -- prints every object type's resolved ancestor, flattened
    /// field list and final VMT slot table (plang/Sema/DumpVmt.h), then
    /// stops, exactly like -dump-ast does for the typed AST.  Exists
    /// because there is no CodeGen for an object type yet (item 2's job),
    /// so nothing an end-to-end compile-and-run test could observe would
    /// prove the VMT slot-assignment algorithm got the right answer.
    DumpVmt,
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
    /// -sanitize-runtime (issue #190 part B option 2): link against
    /// plang_runtime_sanitized (the ASan/UBSan-instrumented runtime variant
    /// runtime/CMakeLists.txt builds only when
    /// -DPLANG_ENABLE_RUNTIME_SANITIZER_TESTS=ON was given at CMake
    /// configure time) instead of the ordinary plang_runtime, and invoke
    /// clang -fsanitize=address,undefined as the final link step so the
    /// ASan/UBSan compiler-rt runtime itself is present too. Strictly
    /// opt-in, off by default, test infrastructure only: an ordinary
    /// `plang foo.pas` never sets this, and the ordinary plang_runtime/
    /// plang_runtime_shared targets remain unconditionally excluded from
    /// sanitizer instrumentation regardless of this flag (see
    /// runtime/CMakeLists.txt's own "-fno-sanitize=all" comment).
    bool         sanitizeRuntime{false};
    /// -static/-dynamic (issue #805): whether the plang runtime is linked
    /// into the final binary as a static archive (libplang.a's own path
    /// embedded directly in the link command, producing a self-contained
    /// binary) or dynamically (-L<libdir> -lplang plus a matching -rpath,
    /// so the binary needs libplang.so/.dylib at run time but not
    /// LD_LIBRARY_PATH/DYLD_LIBRARY_PATH to find it). Dynamic is the
    /// default, matching what a plain `-lplang` workaround already resolved
    /// to before these flags existed; whichever of -static/-dynamic appears
    /// last on the command line wins, the same "last flag wins" rule
    /// -frange-checks/-fno-range-checks and -fnil-checks/-fno-nil-checks
    /// already use.
    bool         linkRuntimeStatic{false};
    bool         debug{false};             ///< -g: generate debug information
    std::string  target;                   ///< --target=<triple>
    std::string  std;                      ///< -std=<dialect>
    bool         suppressWarnings{false};  ///< -w
    bool         warningsAsErrors{false};  ///< -Werror
    /// -frange-checks/-fno-range-checks sets this explicitly; unset (the
    /// default) means "whatever the active dialect starts with" -- ISO 7185
    /// and Extended Pascal on, Turbo off ({$R-}, matching real Turbo
    /// Pascal) -- resolved in makeFEArgs, once -std is known, since the two
    /// may arrive in either order on the command line.  A plain bool here
    /// could not tell "the user asked for checks on" apart from "nothing
    /// was said and true is just the field's own default", which is what
    /// silently dropped an explicit -frange-checks under -std=turbo: this
    /// struct's old default of true and Turbo's own true-when-explicit both
    /// looked identical to makeFEArgs's forwarding check.
    std::optional<bool> rangeChecks;
    bool         nilChecks{true};          ///< -fno-nil-checks disables
    std::vector<std::string> frontendArgs; ///< options passed straight to -pc1
    std::vector<std::string> linkerArgs;   ///< -Wl,… / -Xlinker / -L / -l extras
    std::vector<std::string> modulePaths;  ///< -I<dir> module search paths
    std::vector<std::string> extraInputFiles; ///< additional .pas files for multi-file builds
};

/// Orders two GCC/Clang install-directory names (e.g. "9", "12.2.0") the way
/// their version numbers actually compare, not lexicographically: "9" sorts
/// before "10", and "9.5.0" sorts before "10.1.0" — plain string comparison
/// gets both backwards, since '1' < '9'.  A name that does not parse as a
/// version (the regex [0-9]+(\.[0-9]+){0,3}) sorts before every name that
/// does, so a stray non-version directory is never mistaken for the newest
/// toolchain by a caller that sorts ascending and takes the last entry.
bool versionDirLess(std::string_view A, std::string_view B);

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

    /// What parseArgs found: either a set of options to compile with, or a
    /// request to stop right there -- --version, --help, -dumpversion,
    /// -dumpmachine, --help-warnings all print what they have to print
    /// during parsing itself and have no more work for run() to do
    /// afterward.  EarlyExitCode is a separate field rather than folding
    /// into Options, which describes a compilation and has no business
    /// carrying a short-circuit-and-exit signal of its own.
    ///
    /// parseArgs used to call std::exit() directly for these (issue #174):
    /// Driver::run's own doc comment says it "returns a Unix exit code" to
    /// its caller, which a call to std::exit() half honors and half does
    /// not -- the code is right, but it never reaches run()'s return, or
    /// main()'s.  That is invisible running plang as a subprocess the way
    /// the CLI and this project's own lit suite always do (the process
    /// exits with the same code either way), but not to a host that links
    /// PlangDriver and calls Driver::run() in-process: std::exit() there
    /// tears down that host's whole process mid-call, skipping any cleanup
    /// its own stack frames above run() were relying on, rather than
    /// returning control the way the header promises. Process exit is now
    /// reserved for the executable entry point (tools/driver/driver.cpp's
    /// main, via the exit code run() returns), same as every other outcome
    /// parseArgs can report.
    struct ParseResult {
        Options Opts;
        std::optional<int> EarlyExitCode;
    };

    ParseResult parseArgs(int argc, char *argv[]);
    /// \p isExtraFile is true only for the recursive call this function makes
    /// on itself to compile one of Opts.extraInputFiles to a .o (see the
    /// -save-temps .ll naming in the .cpp file for why that distinction
    /// matters); the top-level call from run() leaves it at the default.
    int         compile(const Options &opts, bool isExtraFile = false);
    std::string defaultOutput(const std::string &inputFile, OutputMode mode);

    std::string findInstallDir() const;
    std::string findSelf()       const;
    static void printVersion();
    static void usage();
};

} // namespace plang
