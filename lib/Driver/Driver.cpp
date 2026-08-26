/// plang driver — orchestrates the compilation pipeline.
///
/// Invokes the plang front end (by re-invoking itself with \c -pc1), llc for
/// code generation, and the platform linker for linking: ld.lld against the
/// GCC startup files on ELF targets, and the macOS system ld against the SDK
/// on Darwin.  The front-end code lives in the shared plang-frontend library;
/// the driver links against it so \c -pc1 mode can be handled in-process
/// without spawning a subprocess for trivial uses.

#include "plang/Driver/Driver.h"
#include "plang/Driver/Options.h"
#include "plang/Frontend/Frontend.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/DiagnosticPrinter.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/MessageCatalog.h"
#include "plang/Basic/Version.h"

#include <algorithm>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace plang;

// ---------------------------------------------------------------------------
// Version / target constants
// ---------------------------------------------------------------------------

static std::string hostTriple() {
    return llvm::sys::getDefaultTargetTriple();
}

/// The triple plang is generating code for: --target= if it was given, and the
/// host otherwise.
static llvm::Triple targetTriple(const Options &Opts) {
    return llvm::Triple(Opts.target.empty()
                            ? hostTriple()
                            : llvm::Triple::normalize(Opts.target));
}

static std::string_view threadModel() {
    llvm::Triple T(hostTriple());
    return T.isOSWindows() ? "win32" : "posix";
}

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

static bool isRegFile(const llvm::Twine &Path) {
    return llvm::sys::fs::is_regular_file(Path);
}

static bool isDir(const llvm::Twine &Path) {
    return llvm::sys::fs::is_directory(Path);
}

/// Runs Prog and returns what it wrote to its standard output, with trailing
/// whitespace removed; "" if it could not be run or did not succeed.
///
/// This is for asking the toolchain about itself — where the SDK is, where the
/// linker is — rather than for running a stage of the compilation, which goes
/// through Driver::runTool so that it is reported by -v and -### and so that a
/// failure to run it is a diagnostic.  Here a failure is not an error: every
/// caller has somewhere else to look.
static std::string captureOutput(const std::string &Prog,
                                 llvm::ArrayRef<llvm::StringRef> Args) {
    auto ResolvedOrErr = llvm::sys::findProgramByName(Prog);
    if (!ResolvedOrErr) return "";

    llvm::SmallString<128> OutPath;
    int Fd;
    if (llvm::sys::fs::createTemporaryFile("plang-probe", "txt", Fd, OutPath))
        return "";
    close(Fd);

    llvm::SmallVector<llvm::StringRef, 8> Argv;
    Argv.push_back(*ResolvedOrErr);
    Argv.append(Args.begin(), Args.end());

    // An empty path is /dev/null, so the probe's own standard input and error
    // stay out of plang's.
    std::optional<llvm::StringRef> Redirects[3] = {
        llvm::StringRef(""), llvm::StringRef(OutPath), llvm::StringRef("")};

    std::string Out;
    if (llvm::sys::ExecuteAndWait(*ResolvedOrErr, Argv, /*Env=*/std::nullopt,
                                  Redirects) == 0) {
        if (auto Buf = llvm::MemoryBuffer::getFile(OutPath))
            Out = (*Buf)->getBuffer().rtrim().str();
    }
    llvm::sys::fs::remove(OutPath);
    return Out;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void Driver::configureDiagnostics(int Argc, char *Argv[]) {
    DiagnosticOptions DO;
    ColorDiagnostics  Color = ColorDiagnostics::Auto;
    std::string_view  Lang;
    bool              ShowFuzzy = false;

    for (int I = 1; I < Argc; ++I) {
        const std::string_view Arg = Argv[I];
        if (Arg.starts_with("-fdiagnostics-language="))
            Lang = Arg.substr(std::string_view("-fdiagnostics-language=").size());
        else if (Arg == "-fdiagnostics-show-fuzzy") ShowFuzzy = true;
        else if (Arg == "-w")          DO.SuppressWarnings = true;
        else if (Arg == "-Werror")     DO.WarningsAsErrors = true;
        else if (Arg == "-Wall")       DO.DisabledWarnings.clear();
        else if (Arg == "-Wno-all")    DO.SuppressWarnings = true;
        else if (Arg.starts_with("-Wno-") && Arg.size() > 5)
            DO.DisabledWarnings.emplace_back(Arg.substr(5));
        else if (Arg.starts_with("-W") && Arg.size() > 2 && !Arg.starts_with("-Wl,"))
            std::erase(DO.DisabledWarnings, std::string(Arg.substr(2)));
        else if (auto C = colorDiagnosticsArg(Arg); C != ColorDiagnostics::Auto)
            Color = C;
    }

    // A name no warning answers to is not reported here.  The front end parses
    // the same -W options and reports it there, and every one of them reaches
    // the front end; reporting it in both places would say it twice.
    // Before setOptions, because the very next thing the driver does is parse
    // the rest of the command line and report what is wrong with it.  A locale
    // chosen any later would leave those first messages in English and the
    // rest translated.
    (void)selectLocale(Lang, findInstallDir(), ShowFuzzy);

    Diags_.setOptions(std::move(DO));
    UseColor_ = useColor(Color, llvm::sys::Process::StandardErrIsDisplayed());
}

void Driver::diag(DiagID ID, std::initializer_list<std::string_view> Args) {
    // False when -w, -Wno-<name> or the error limit discarded it.
    if (!Diags_.report(SourceLocation(), ID, Args)) return;

    const DiagnosticPrinter P(SrcMgr_, UseColor_, /*ShowCarets=*/false, "plang");
    std::cerr << P.print(Diags_.diagnostics().back()) << "\n";
}

// ---------------------------------------------------------------------------
// Driver constructor — resolve the executable path portably
// ---------------------------------------------------------------------------

// A free function in this translation unit whose address is a valid symbol hint
// for llvm::sys::fs::getMainExecutable on macOS (which uses dladdr internally).
static void executableAnchor() {}

Driver::Driver(const char *Argv0) {
    // getMainExecutable uses /proc/self/exe on Linux, _NSGetExecutablePath on
    // macOS, and GetModuleFileName on Windows.  The second arg must be the
    // address of any symbol in the main executable image.
    ExePath_ = llvm::sys::fs::getMainExecutable(
        Argv0, reinterpret_cast<void *>(&executableAnchor));
    if (ExePath_.empty() && Argv0)
        ExePath_ = Argv0; // last-resort fallback
}

// ---------------------------------------------------------------------------
// Self-path and install-dir helpers
// ---------------------------------------------------------------------------

std::string Driver::findSelf() const {
    return ExePath_.empty() ? "plang" : ExePath_;
}

std::string Driver::findInstallDir() const {
    if (ExePath_.empty()) return ".";
    llvm::SmallString<256> Dir(ExePath_);
    llvm::sys::path::remove_filename(Dir);
    return std::string(Dir);
}

/// Returns the path to libplang.a: checks PREFIX/lib at runtime first
/// (installed layout), then falls back to the baked-in build-directory path.
static std::string findRuntimeLib(const std::string &ExePath) {
    if (!ExePath.empty()) {
        llvm::SmallString<256> BinDir(ExePath);
        llvm::sys::path::remove_filename(BinDir);
        llvm::SmallString<256> Prefix(BinDir);
        llvm::sys::path::remove_filename(Prefix);
        llvm::SmallString<256> Candidate(Prefix);
        llvm::sys::path::append(Candidate, "lib", "libplang.a");
        if (isRegFile(Candidate)) return std::string(Candidate);
    }
#ifdef PLANG_RUNTIME_DIR
    {
        std::string Fallback = PLANG_RUNTIME_DIR "/libplang.a";
        if (isRegFile(Fallback)) return Fallback;
    }
#endif
    return "";
}

// ---------------------------------------------------------------------------
// GCC installation detection (for ld.lld CRT/library paths)
// ---------------------------------------------------------------------------

struct GCCInstall {
    std::string Dir;    ///< versioned GCC lib dir
    std::string LibDir; ///< system lib dir containing Scrt1.o
};

static std::vector<std::string> subdirs(const std::string &Parent) {
    std::vector<std::string> Out;
    std::error_code EC;
    for (llvm::sys::fs::directory_iterator It(Parent, EC), End;
         !EC && It != End; It.increment(EC)) {
        llvm::StringRef Name = llvm::sys::path::filename(It->path());
        if (!Name.empty() && Name[0] != '.')
            Out.push_back(std::string(Name));
    }
    std::sort(Out.begin(), Out.end());
    return Out;
}

static GCCInstall detectGCC() {
    llvm::Triple Host(hostTriple());
    std::string  Arch = Host.getArchName().str();

    std::vector<std::string> Triples = {
        Arch + "-pc-linux-gnu",
        Arch + "-linux-gnu",
        Arch + "-redhat-linux",
        Arch + "-suse-linux",
    };
    std::string HostStr = hostTriple();
    bool Known = false;
    for (const auto &T : Triples) if (T == HostStr) { Known = true; break; }
    if (!Known) Triples.push_back(HostStr);

    static const char *Prefixes[] = {
        "/usr/lib64/gcc", "/usr/lib/gcc", "/usr/local/lib/gcc", nullptr,
    };
    std::vector<std::string> LibCandidates = {
        "/usr/lib64",
        "/usr/lib/" + Arch + "-linux-gnu",
        "/usr/lib",
    };

    GCCInstall Best;
    for (const char **Pfx = Prefixes; *Pfx && Best.Dir.empty(); ++Pfx) {
        for (const auto &Tri : Triples) {
            if (!Best.Dir.empty()) break;
            std::string TriDir = std::string(*Pfx) + "/" + Tri;
            if (!isDir(TriDir)) continue;
            // subdirs() returns by value, so the vector must outlive the loop;
            // iterating temporaries directly leaves both iterators dangling and
            // compares positions in two unrelated containers.
            const std::vector<std::string> Subs = subdirs(TriDir);
            for (auto It = Subs.rbegin(), End = Subs.rend(); It != End; ++It) {
                std::string VerDir = TriDir + "/" + *It;
                if (isRegFile(VerDir + "/crtbeginS.o")) {
                    Best.Dir = VerDir;
                    break;
                }
            }
        }
    }
    for (const auto &L : LibCandidates)
        if (isRegFile(L + "/Scrt1.o")) { Best.LibDir = L; break; }

    return Best;
}

static std::string linkerEmulation(const llvm::Triple &T) {
    switch (T.getArch()) {
    case llvm::Triple::x86_64:  return "elf_x86_64";
    case llvm::Triple::x86:     return "elf_i386";
    case llvm::Triple::aarch64: return "aarch64linux";
    case llvm::Triple::riscv64: return "elf64lriscv";
    case llvm::Triple::ppc64le: return "elf64lppc";
    default:                    return "";
    }
}

static std::string dynamicLinker(const llvm::Triple &T) {
    switch (T.getArch()) {
    case llvm::Triple::x86_64:  return "/lib64/ld-linux-x86-64.so.2";
    case llvm::Triple::x86:     return "/lib/ld-linux.so.2";
    case llvm::Triple::aarch64: return "/lib/ld-linux-aarch64.so.1";
    case llvm::Triple::riscv64: return "/lib/ld-linux-riscv64-lp64d.so.1";
    case llvm::Triple::ppc64le: return "/lib64/ld64.so.2";
    default:                    return "";
    }
}

// ---------------------------------------------------------------------------
// Darwin toolchain detection (SDK, linker and compiler builtins)
// ---------------------------------------------------------------------------

/// Where the parts of the macOS toolchain a link needs are.
struct DarwinToolchain {
    std::string Linker;      ///< the ld to run
    std::string SDKPath;     ///< -syslibroot; libSystem and the rest live here
    std::string SDKVersion;  ///< as recorded in the SDK, for -platform_version
    std::string BuiltinsLib; ///< libclang_rt.osx.a, "" if it was not found
};

/// The name ld knows an architecture by, which is not always the name the
/// triple does: an AArch64 triple may be spelled either "arm64" or "aarch64",
/// and ld only answers to the first.
static std::string machOArchName(const llvm::Triple &T) {
    switch (T.getArch()) {
    case llvm::Triple::aarch64:    return T.isArm64e() ? "arm64e" : "arm64";
    case llvm::Triple::aarch64_32: return "arm64_32";
    case llvm::Triple::x86_64:     return "x86_64";
    case llvm::Triple::x86:        return "i386";
    default:                       return "";
    }
}

/// The macOS an executable is being built for.
///
/// This ends up in the object file, put there by llc from the triple, and in
/// the executable, put there by ld from -platform_version.  ld warns when the
/// two disagree, so both come from here.
static std::string darwinDeploymentTarget(const llvm::Triple &T) {
    // The variable every other macOS toolchain reads, and it wins: someone who
    // has set it has said which macOS they mean.
    if (const char *E = getenv("MACOSX_DEPLOYMENT_TARGET"); E && *E) return E;

    llvm::VersionTuple V;
    // Both asks are guarded by isMacOSX because getMacOSXVersion is only
    // answerable for macOS and asserts rather than declining for anything
    // else — the host when plang is cross-compiling from Linux, the target
    // when it names one of the other Darwin platforms.
    //
    // A triple with no version on it — a bare "arm64-apple-darwin" — is read
    // as the oldest macOS LLVM knows, which is not what anyone writing it
    // meant.  Only a triple that names a version answers for itself.
    if (T.isMacOSX() && !T.getOSVersion().empty() && T.getMacOSXVersion(V))
        return V.getAsString();
    if (const llvm::Triple Host(hostTriple());
        Host.isMacOSX() && Host.getMacOSXVersion(V))
        return V.getAsString();
    return "";
}

/// The version the SDK says it is, from the SDKSettings.json beside it.
static std::string sdkVersion(const std::string &SDKPath) {
    if (SDKPath.empty()) return "";
    auto Buf = llvm::MemoryBuffer::getFile(SDKPath + "/SDKSettings.json");
    if (!Buf) return "";
    auto Parsed = llvm::json::parse((*Buf)->getBuffer());
    if (!Parsed) { llvm::consumeError(Parsed.takeError()); return ""; }
    if (const llvm::json::Object *Root = Parsed->getAsObject())
        if (auto V = Root->getString("Version")) return V->str();
    return "";
}

/// <Root>/lib/clang/<version>/lib/darwin/libclang_rt.osx.a, newest version
/// first, where Root is the usr directory of a toolchain.
static std::string builtinsUnder(llvm::StringRef Root) {
    const std::string ClangDir = Root.str() + "/lib/clang";
    if (!isDir(ClangDir)) return "";
    const std::vector<std::string> Versions = subdirs(ClangDir);
    for (auto It = Versions.rbegin(), End = Versions.rend(); It != End; ++It) {
        const std::string Candidate =
            ClangDir + "/" + *It + "/lib/darwin/libclang_rt.osx.a";
        if (isRegFile(Candidate)) return Candidate;
    }
    return "";
}

static DarwinToolchain detectDarwinToolchain() {
    DarwinToolchain TC;

    // xcrun answers for whichever toolchain xcode-select points at, which is
    // the one the SDK belongs to; everything after it here is for a machine
    // where xcrun cannot be run.  Asking it for ld rather than taking the
    // first one on PATH also keeps a GNU ld installed alongside — Homebrew's
    // binutils puts one there — from being handed a Mach-O link.
    TC.Linker = captureOutput("xcrun", {"-f", "ld"});
    if (TC.Linker.empty() || !isRegFile(TC.Linker)) TC.Linker = "ld";

    if (const char *E = getenv("SDKROOT"); E && *E && isDir(E))
        TC.SDKPath = E;
    else
        TC.SDKPath = captureOutput("xcrun", {"--sdk", "macosx", "--show-sdk-path"});
    if (!isDir(TC.SDKPath)) {
        TC.SDKPath.clear();
        static const char *Fallbacks[] = {
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
            "/Applications/Xcode.app/Contents/Developer/Platforms"
            "/MacOSX.platform/Developer/SDKs/MacOSX.sdk",
        };
        for (const char *F : Fallbacks)
            if (isDir(F)) { TC.SDKPath = F; break; }
    }
    TC.SDKVersion = sdkVersion(TC.SDKPath);

    if (const char *E = getenv("PLANG_BUILTINS_LIB"); E && *E) {
        TC.BuiltinsLib = E;
    } else {
        // The builtins sit beside the linker: both an Xcode toolchain and the
        // Command Line Tools keep ld in <root>/usr/bin and the archive in
        // <root>/usr/lib/clang/<version>/lib/darwin.
        if (TC.Linker != "ld")
            TC.BuiltinsLib = builtinsUnder(llvm::sys::path::parent_path(
                llvm::sys::path::parent_path(TC.Linker)));
        static const char *Roots[] = {
            "/Library/Developer/CommandLineTools/usr",
            "/Applications/Xcode.app/Contents/Developer/Toolchains"
            "/XcodeDefault.xctoolchain/usr",
        };
        for (const char *R : Roots) {
            if (!TC.BuiltinsLib.empty()) break;
            TC.BuiltinsLib = builtinsUnder(R);
        }
    }
    return TC;
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

void Driver::printVersion() {
    const std::string Dir = Driver().findInstallDir();
    std::println("plang version {}\nTarget: {}\nThread model: {}\nInstalledDir: {}",
                 PLANG_VERSION_STRING, hostTriple(), threadModel(), Dir);
    std::println("Messages: {}", describeLocale());
}

void Driver::usage() {
    std::cerr <<
        "OVERVIEW: plang LLVM Pascal compiler\n"
        "\n"
        "USAGE: plang [options] file...\n"
        "\n"
        "OPTIONS:\n"
        << opts::helpText(opts::Consumer::Both);
}

// ---------------------------------------------------------------------------
// File-name helpers
// ---------------------------------------------------------------------------

static std::string stem(const std::string &Path) {
    auto Slash = Path.rfind('/');
    std::string Base = (Slash == std::string::npos) ? Path : Path.substr(Slash + 1);
    auto Dot = Base.rfind('.');
    return (Dot == std::string::npos) ? Base : Base.substr(0, Dot);
}

/// Like stem(), but keeps the directory instead of discarding it -- flattened
/// into the same path component by turning every '/' into '_', e.g.
/// "unitA/foo.pas" -> "unitA_foo".  Two extra input files that share a
/// basename in different directories (issue #20), such as "unitA/foo.pas" and
/// "unitB/foo.pas", must not both default to the bare stem() "foo": that
/// collides in the cwd, silently overwriting one's default output (.o, or the
/// -save-temps .ll) with the other's.  Used only for an *extra* file's own
/// default output names, never for the main file's (defaultOutput, above):
/// the main file has no sibling to collide with under its own bare stem, and
/// changing its long-established naming is out of scope here.  Still lands
/// flat in the cwd, matching every other default output name in this file --
/// this is not a build-directory mechanism, just a wider stem.
static std::string flattenedStem(const std::string &Path) {
    auto Slash = Path.rfind('/');
    if (Slash == std::string::npos) return stem(Path);
    std::string Dir = Path.substr(0, Slash);
    for (char &C : Dir) if (C == '/') C = '_';
    return Dir + "_" + stem(Path);
}

std::string Driver::defaultOutput(const std::string &InputFile, OutputMode Mode) {
    switch (Mode) {
        case OutputMode::Executable: return "a.out";
        case OutputMode::Assembly:   return stem(InputFile) + ".s";
        case OutputMode::Object:     return stem(InputFile) + ".o";
        case OutputMode::LLVMIr:     return stem(InputFile) + ".ll";
        case OutputMode::DumpAst:    return "";
        case OutputMode::DumpTokens:    return "";
        case OutputMode::DumpParseTree: return "";
    }
    return "a.out";
}

// ---------------------------------------------------------------------------
// Subprocess runner — no shell, no quoting required
// ---------------------------------------------------------------------------

int Driver::runTool(const std::string &Prog,
                    const std::vector<std::string> &Args,
                    bool Verbose, bool DryRun) {
    if (Verbose || DryRun) {
        std::cerr << Prog;
        for (const auto &A : Args) std::cerr << ' ' << A;
        std::cerr << '\n';
    }
    if (DryRun) return 0;

    // Resolve to an absolute path so ExecuteAndWait can find the binary.
    auto ResolvedOrErr = llvm::sys::findProgramByName(Prog);
    if (!ResolvedOrErr) {
        diag(diag::err_cannot_find_program,
             {Prog, ResolvedOrErr.getError().message()});
        return 1;
    }
    const std::string &Resolved = *ResolvedOrErr;

    // Build the StringRef argv: argv[0] = program path, then the rest.
    llvm::SmallVector<llvm::StringRef, 32> Argv;
    Argv.push_back(Resolved);
    for (const auto &A : Args) Argv.push_back(A);

    std::string ErrMsg;
    bool ExecFailed = false;
    int Rc = llvm::sys::ExecuteAndWait(Resolved, Argv,
        /*Env=*/std::nullopt, /*Redirects=*/{},
        /*SecondsToWait=*/0, /*MemoryLimit=*/0,
        &ErrMsg, &ExecFailed);
    if (ExecFailed) {
        diag(diag::err_program_exec_failed, {Prog, ErrMsg});
        return 1;
    }
    // llvm answers -1 for a failure to run and -2 for a child killed by a
    // signal, and NEITHER sets ExecFailed.  Returning that straight through
    // made the driver exit 254 in silence whenever the compiler crashed --
    // which is how a segfault in codegen came to look like a diagnostic-free
    // refusal to compile.  Say what happened, and exit 1 like every other
    // error, so a caller cannot read the crash as an ordinary rejection.
    if (Rc < 0) {
        diag(diag::err_program_crashed,
             {Prog, ErrMsg.empty() ? "terminated by a signal" : ErrMsg});
        return 1;
    }
    return Rc;
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

Options Driver::parseArgs(int Argc, char *Argv[]) {
    Options Opts;
    bool HasInput = false;

    for (int I = 1; I < Argc; ++I) {
        std::string Arg = Argv[I];

        if (Arg == "--version") {
            printVersion(); std::exit(0);
        } else if (Arg == "-dumpversion") {
            std::cout << PLANG_VERSION_STRING << "\n"; std::exit(0);
        } else if (Arg == "-dumpmachine") {
            std::cout << hostTriple() << "\n"; std::exit(0);
        } else if (Arg == "-h" || Arg == "--help") {
            usage(); std::exit(0);
        } else if (Arg == "--help-warnings") {
            std::println("Warnings, all enabled by default.  Turn one off with");
            std::println("-Wno-<name>, or all of them with -w.\n");
            forEachWarningName([](const std::string &N) {
                std::println("  -Wno-{}", N);
            });
            std::exit(0);

        } else if (Arg == "-###") {
            Opts.dryRun = true;
        } else if (Arg == "-v") {
            Opts.verbose = true;
        } else if (Arg == "-save-temps") {
            Opts.saveTemps = true;

        } else if (Arg == "-S") {
            Opts.mode = OutputMode::Assembly;
        } else if (Arg == "-c") {
            Opts.mode = OutputMode::Object;
        } else if (Arg == "-emit-llvm") {
            Opts.mode = OutputMode::LLVMIr;
        } else if (Arg == "-dump-ast") {
            Opts.mode = OutputMode::DumpAst;
        } else if (Arg == "-dump-tokens") {
            Opts.mode = OutputMode::DumpTokens;
        } else if (Arg == "-dump-parse-tree") {
            Opts.mode = OutputMode::DumpParseTree;
        } else if (Arg == "-o") {
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-o"}); continue; }
            Opts.outputFile = Argv[++I];

        } else if (Arg == "-O0") { Opts.optLevel = 0;
        } else if (Arg == "-O1") { Opts.optLevel = 1;
        } else if (Arg == "-O2") { Opts.optLevel = 2;
        } else if (Arg == "-O3") { Opts.optLevel = 3;

        } else if (Arg == "-g") {
            Opts.debug = true;
        } else if (Arg.starts_with("--target=")) {
            Opts.target = Arg.substr(9);
        } else if (Arg.starts_with("-fdiagnostics-language=") ||
                   Arg == "-fdiagnostics-show-fuzzy") {
            // Read by configureDiagnostics before parsing began; forwarded below.
            Opts.frontendArgs.push_back(Arg);
        } else if (Arg.starts_with("-std=")) {
            Opts.std = Arg.substr(5);

        } else if (Arg == "-w") {
            Opts.suppressWarnings = true;
        } else if (Arg == "-Werror") {
            Opts.warningsAsErrors = true;
        } else if (Arg == "-frange-checks") {
            Opts.rangeChecks = true;
        } else if (Arg == "-fno-range-checks") {
            Opts.rangeChecks = false;
        } else if (Arg == "-fnil-checks") {
            Opts.nilChecks = true;
        } else if (Arg == "-fno-nil-checks") {
            Opts.nilChecks = false;
        } else if (Arg.starts_with("-W") && Arg.size() > 2) {
            if (Arg.size() <= 3 || Arg[2] != 'l' || Arg[3] != ',') {
                Opts.frontendArgs.push_back(Arg);
            } else {
                // gcc/clang convention: everything after "-Wl," is a
                // comma-separated list of arguments to hand the linker
                // verbatim, each becoming its own argv entry — not the
                // literal "-Wl,..." string, which ld.lld (invoked
                // directly, with no gcc in between to strip it) rejects
                // outright as an unknown argument.
                std::string_view Rest(Arg.data() + 4, Arg.size() - 4);
                size_t Pos = 0;
                while (Pos <= Rest.size()) {
                    size_t Comma = Rest.find(',', Pos);
                    if (Comma == std::string_view::npos) {
                        Opts.linkerArgs.emplace_back(Rest.substr(Pos));
                        break;
                    }
                    Opts.linkerArgs.emplace_back(Rest.substr(Pos, Comma - Pos));
                    Pos = Comma + 1;
                }
            }

        } else if (Arg == "-Xlinker") {
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-Xlinker"}); continue; }
            // Forward the single following argument as-is: unlike -Wl,,
            // gcc/clang do not comma-split it, and the "-Xlinker" marker
            // itself is never passed to the linker.
            Opts.linkerArgs.push_back(Argv[++I]);
        } else if (Arg.size() > 2 && Arg[0] == '-' && Arg[1] == 'I') {
            Opts.modulePaths.push_back(Arg.substr(2));
        } else if (Arg == "-I") {
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-I"}); continue; }
            Opts.modulePaths.push_back(Argv[++I]);
        } else if (Arg.size() > 2 && Arg[0] == '-' && Arg[1] == 'L') {
            Opts.linkerArgs.push_back(Arg);
        } else if (Arg.size() > 2 && Arg[0] == '-' && Arg[1] == 'l') {
            Opts.linkerArgs.push_back(Arg);

        } else if (const opts::Option *O = opts::lookup(Arg);
                   O && opts::goesToFrontend(*O)) {
            // An option the front end knows about that the driver has nothing
            // of its own to do with.  Pass it on rather than dropping it: the
            // two used to keep separate lists, and anything added to only one
            // of them was silently discarded here.
            Opts.frontendArgs.push_back(Arg);
            if (opts::takesSeparateValue(*O, Arg)) {
                if (I + 1 >= Argc) {
                    diag(diag::err_arg_requires_value, {Arg});
                    continue;
                }
                Opts.frontendArgs.push_back(Argv[++I]);
            }
        } else if (!Arg.empty() && Arg[0] == '-') {
            diag(diag::warn_unrecognized_argument, {Arg});
        } else {
            // Detect file type by extension: .o / .a → pass to linker directly.
            bool IsObjOrLib = (Arg.size() >= 2 &&
                               (Arg.substr(Arg.size()-2) == ".o" ||
                                (Arg.size() >= 2 && Arg.substr(Arg.size()-2) == ".a")));
            if (IsObjOrLib) {
                Opts.linkerArgs.push_back(Arg);
            } else if (!HasInput) {
                Opts.inputFile = Arg;
                HasInput = true;
            } else {
                // Additional .pas files for multi-file builds.
                Opts.extraInputFiles.push_back(Arg);
            }
        }
    }

    if (!HasInput) Opts.inputFile.clear();
    return Opts;
}

// ---------------------------------------------------------------------------
// Compilation pipeline
// ---------------------------------------------------------------------------

/// The triple to hand llc, or "" to leave it the one the IR names.
///
/// On macOS this is where the deployment target is settled.  The front end
/// puts the host triple in the IR, which carries the Darwin kernel version
/// rather than the macOS one and says nothing about which macOS the program is
/// meant to run on; naming the deployment target here puts it in the object
/// file, where it has to match what -platform_version tells ld or ld warns
/// about every object it reads.
static std::string llcTriple(const Options &Opts) {
    const llvm::Triple T = targetTriple(Opts);
    if (T.isMacOSX()) {
        if (const std::string MinOS = darwinDeploymentTarget(T); !MinOS.empty()) {
            llvm::Triple Versioned(T);
            Versioned.setOSName("macosx" + MinOS);
            return Versioned.str();
        }
    }
    return Opts.target;
}

/// Build the argv for a -pc1 front-end invocation.  \p DumpFlag, if given, is
/// one of the front-end-only dump modes ("-dump-ast", "-dump-tokens",
/// "-dump-parse-tree") that stop the pipeline before code generation.
static std::vector<std::string> makeFEArgs(const Options &Opts,
                                            const std::string &Out,
                                            const char *DumpFlag = nullptr) {
    std::vector<std::string> Args = {"-pc1"};
    if (!Out.empty())          { Args.push_back("-o"); Args.push_back(Out); }
    if (DumpFlag)                Args.push_back(DumpFlag);
    Args.push_back(Opts.inputFile);
    if (!Opts.std.empty())     { Args.push_back("-std=" + Opts.std); }
    if (Opts.suppressWarnings)   Args.push_back("-w");
    if (Opts.warningsAsErrors)   Args.push_back("-Werror");
    if (!Opts.rangeChecks)       Args.push_back("-fno-range-checks");
    if (!Opts.nilChecks)         Args.push_back("-fno-nil-checks");
    // The front end runs the LLVM pipeline; llc gets the same level separately
    // for instruction selection.
    Args.push_back("-O" + std::to_string(Opts.optLevel));
    if (Opts.debug)              Args.push_back("-g");
    for (const auto &A : Opts.frontendArgs) Args.push_back(A);
    for (const auto &P : Opts.modulePaths) Args.push_back("-I" + P);
    return Args;
}

/// Link ObjFile into OutFile using ld.lld with detected GCC CRT paths.
///
/// Takes the Driver so that it can report through the same engine everything
/// else does, and run ld.lld through the same runTool.
static int linkELF(Driver &D, const Options &Opts, const std::string &ObjFile,
                   const std::string &OutFile,
                   const std::string &RuntimeLib,
                   bool Verbose, bool DryRun) {
    GCCInstall GCC = detectGCC();

    if (const char *E = getenv("PLANG_GCC_DIR"))  GCC.Dir    = E;
    if (const char *E = getenv("PLANG_LIB_DIR"))  GCC.LibDir = E;

    if (GCC.Dir.empty()) {
        D.diag(diag::err_no_gcc_installation);
        return 1;
    }
    if (GCC.LibDir.empty()) {
        D.diag(diag::err_no_c_library_dir);
        return 1;
    }

    const llvm::Triple T = targetTriple(Opts);
    std::string Emul = linkerEmulation(T);
    std::string DynL = dynamicLinker(T);
    std::string Arch = T.getArchName().str();

    std::vector<std::string> Args;
    Args.push_back("--hash-style=gnu");
    Args.push_back("--build-id");
    Args.push_back("--eh-frame-hdr");
    if (!Emul.empty()) { Args.push_back("-m"); Args.push_back(Emul); }
    Args.push_back("-pie");
    if (!DynL.empty()) { Args.push_back("-dynamic-linker"); Args.push_back(DynL); }
    Args.push_back("-o"); Args.push_back(OutFile);
    Args.push_back(GCC.LibDir + "/Scrt1.o");
    Args.push_back(GCC.LibDir + "/crti.o");
    Args.push_back(GCC.Dir    + "/crtbeginS.o");
    Args.push_back("-L" + GCC.Dir);
    Args.push_back("-L" + GCC.LibDir);
    Args.push_back("-L/lib64");
    Args.push_back("-L/usr/lib64");
    Args.push_back("-L/lib");
    Args.push_back("-L/usr/lib");
    Args.push_back("-L/usr/lib/" + Arch + "-linux-gnu");
    Args.push_back(ObjFile);
    if (!RuntimeLib.empty()) Args.push_back(RuntimeLib);
    Args.push_back("-lm");
    Args.push_back("-lgcc"); Args.push_back("--as-needed");
    Args.push_back("-lgcc_s"); Args.push_back("--no-as-needed");
    Args.push_back("-lc");
    Args.push_back("-lgcc"); Args.push_back("--as-needed");
    Args.push_back("-lgcc_s"); Args.push_back("--no-as-needed");
    for (const auto &A : Opts.linkerArgs) Args.push_back(A);
    Args.push_back(GCC.Dir    + "/crtendS.o");
    Args.push_back(GCC.LibDir + "/crtn.o");

    return D.runTool("ld.lld", Args, Verbose, DryRun);
}

/// Link ObjFile into OutFile using the macOS system linker.
///
/// Much shorter than the ELF link because macOS has no startup files to find:
/// libSystem carries the entry point, and is libc, libm and libpthread in one,
/// so the whole system side of the link is -lSystem inside the SDK.  What is
/// left is telling ld where the SDK is, which macOS the executable is for, and
/// where the compiler builtins are — the runtime's complex arithmetic calls
/// __muldc3 and __divdc3, which the compiler emits calls to rather than code
/// for, and which on Linux arrive with -lgcc.
static int linkDarwin(Driver &D, const Options &Opts, const std::string &ObjFile,
                      const std::string &OutFile,
                      const std::string &RuntimeLib,
                      bool Verbose, bool DryRun) {
    const llvm::Triple T  = targetTriple(Opts);
    const DarwinToolchain TC = detectDarwinToolchain();

    if (TC.SDKPath.empty()) {
        D.diag(diag::err_no_macos_sdk);
        return 1;
    }

    const std::string MinOS = darwinDeploymentTarget(T);

    std::vector<std::string> Args;
    if (std::string Arch = machOArchName(T); !Arch.empty()) {
        Args.push_back("-arch"); Args.push_back(Arch);
    }
    Args.push_back("-syslibroot"); Args.push_back(TC.SDKPath);
    if (!MinOS.empty()) {
        Args.push_back("-platform_version");
        Args.push_back("macos");
        Args.push_back(MinOS);
        // An SDK that does not say which version it is is reported as the
        // deployment target, which ld accepts and which says no more than it
        // can tell.
        Args.push_back(TC.SDKVersion.empty() ? MinOS : TC.SDKVersion);
    }
    Args.push_back("-o"); Args.push_back(OutFile);
    Args.push_back(ObjFile);
    if (!RuntimeLib.empty()) Args.push_back(RuntimeLib);
    for (const auto &A : Opts.linkerArgs) Args.push_back(A);
    Args.push_back("-lSystem");
    // Left out rather than reported when it is missing: it is only wanted by
    // programs that do complex arithmetic, and everything else links without.
    if (!TC.BuiltinsLib.empty()) Args.push_back(TC.BuiltinsLib);

    return D.runTool(TC.Linker, Args, Verbose, DryRun);
}

/// Link ObjFile into OutFile with whichever linker the target calls for.
static int link(Driver &D, const Options &Opts, const std::string &ObjFile,
                const std::string &OutFile, const std::string &RuntimeLib,
                bool Verbose, bool DryRun) {
    if (targetTriple(Opts).isOSDarwin())
        return linkDarwin(D, Opts, ObjFile, OutFile, RuntimeLib, Verbose, DryRun);
    return linkELF(D, Opts, ObjFile, OutFile, RuntimeLib, Verbose, DryRun);
}

int Driver::compile(const Options &Opts, bool IsExtraFile) {
    const std::string OutFile = Opts.outputFile.empty()
        ? defaultOutput(Opts.inputFile, Opts.mode)
        : Opts.outputFile;

    const std::string Self = findSelf();
    const std::string OOpt = "-O" + std::to_string(Opts.optLevel);
    const bool V  = Opts.verbose;
    const bool DR = Opts.dryRun;

    // Check the input file before spawning the front end.
    if (!llvm::sys::fs::exists(Opts.inputFile)) {
        diag(diag::err_file_not_found, {Opts.inputFile});
        return 1;
    }

    // Every extra file's own directory, deduplicated, computed once up front:
    // both the extra-file compile loop below and the main file's own compile
    // options (further down) need the *full* set, not just one file's own
    // directory -- an extra file may import a module defined by a sibling
    // extra file in a different directory (issue #21), and the driver already
    // has everything it needs on the command line to resolve that.
    std::vector<std::string> ExtraDirs;
    for (const auto &ExtraFile : Opts.extraInputFiles) {
        auto Slash = ExtraFile.rfind('/');
        std::string Dir = (Slash == std::string::npos) ? "." : ExtraFile.substr(0, Slash);
        if (std::find(ExtraDirs.begin(), ExtraDirs.end(), Dir) == ExtraDirs.end())
            ExtraDirs.push_back(Dir);
    }

    // Multi-file mode: compile each extra input file to a .o first.
    // Every extra file's directory -- not just its own -- is added to its
    // module search path, so PMI files written alongside any of them,
    // including siblings, are automatically found.
    std::vector<std::string> ExtraObjs;
    for (const auto &ExtraFile : Opts.extraInputFiles) {
        if (!llvm::sys::fs::exists(ExtraFile)) {
            diag(diag::err_file_not_found, {ExtraFile});
            return 1;
        }
        Options ExtraOpts = Opts;
        ExtraOpts.inputFile       = ExtraFile;
        ExtraOpts.mode            = OutputMode::Object;
        // flattenedStem, not stem: two extra files that share a basename in
        // different directories (issue #20) must not default to the same
        // "foo.o" in the cwd, silently clobbering one with the other's.
        ExtraOpts.outputFile      = flattenedStem(ExtraFile) + ".o";
        ExtraOpts.extraInputFiles.clear(); // avoid recursion
        // Add every extra file's directory, not just this one's, so that this
        // file may import a module a sibling extra file defines (issue #21).
        // Avoid duplicates the same way the main-file loop below does.
        for (const auto &Dir : ExtraDirs) {
            bool Already = false;
            for (const auto &P : ExtraOpts.modulePaths) if (P == Dir) { Already = true; break; }
            if (!Already) ExtraOpts.modulePaths.push_back(Dir);
        }
        if (compile(ExtraOpts, /*IsExtraFile=*/true) != 0) return 1;
        ExtraObjs.push_back(ExtraOpts.outputFile);
    }

    // DumpAst/DumpTokens/DumpParseTree modes: front end only.
    if (Opts.mode == OutputMode::DumpAst)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-ast"), V, DR);
    if (Opts.mode == OutputMode::DumpTokens)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-tokens"), V, DR);
    if (Opts.mode == OutputMode::DumpParseTree)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-parse-tree"), V, DR);

    // LLVMIr mode: front end only.
    if (Opts.mode == OutputMode::LLVMIr)
        return runTool(Self, makeFEArgs(Opts, OutFile), V, DR);

    // Choose IR file.
    std::string IrFile;
    bool OwnIr = false;

    if (Opts.saveTemps) {
        // flattenedStem for an extra file, for the same reason as its .o
        // above: two extra files sharing a basename in different directories
        // would otherwise both save to the same "foo.ll", the second
        // silently overwriting the first's kept-for-inspection IR.  The main
        // file's own naming is untouched -- stem(), as always -- since it has
        // no sibling to collide with.
        IrFile = (IsExtraFile ? flattenedStem(Opts.inputFile) : stem(Opts.inputFile)) + ".ll";
    } else {
        llvm::SmallString<128> TmpPath;
        int Fd;
        if (auto EC = llvm::sys::fs::createTemporaryFile("plang", "ll", Fd, TmpPath)) {
            diag(diag::err_cannot_create_temp_file, {EC.message()});
            return 1;
        }
        close(Fd);
        IrFile = std::string(TmpPath);
        OwnIr  = true;
    }

    // Step 1: front end → LLVM IR.
    // For multi-file builds, also add the extra files' directories to the main
    // compilation's module search paths so PMI files are discovered automatically.
    Options MainOpts = Opts;
    for (const auto &Dir : ExtraDirs) {
        // Avoid duplicates: MainOpts starts as a copy of Opts, which may
        // already list one of these via -I.
        bool Already = false;
        for (const auto &P : MainOpts.modulePaths) if (P == Dir) { Already = true; break; }
        if (!Already) MainOpts.modulePaths.push_back(Dir);
    }
    int Rc = runTool(Self, makeFEArgs(MainOpts, IrFile), V, DR);
    if (Rc != 0) {
        if (OwnIr) llvm::sys::fs::remove(IrFile);
        return Rc;
    }

    const std::string LLCTriple = llcTriple(Opts);

    // Assembly mode: IR → .s.
    if (Opts.mode == OutputMode::Assembly) {
        std::vector<std::string> LLCArgs = {OOpt};
        // -g is not forwarded here: debug info travels in the IR's own
        // metadata (the !dbg attachments and the "Debug Info Version"
        // module flag), which llc reads and emits DWARF for on its own --
        // confirmed empirically against this llc, not assumed.
        if (!LLCTriple.empty())    { LLCArgs.push_back("--mtriple"); LLCArgs.push_back(LLCTriple); }
        LLCArgs.push_back("-o"); LLCArgs.push_back(OutFile);
        LLCArgs.push_back(IrFile);
        Rc = runTool("llc", LLCArgs, V, DR);
        if (OwnIr) llvm::sys::fs::remove(IrFile);
        return Rc;
    }

    // Object / Executable: IR → .o.
    std::string ObjFile;
    bool OwnObj = false;

    if (Opts.mode == OutputMode::Object) {
        ObjFile = OutFile;
    } else if (Opts.saveTemps) {
        ObjFile = stem(Opts.inputFile) + ".o";
    } else {
        llvm::SmallString<128> TmpPath;
        int Fd;
        if (auto EC = llvm::sys::fs::createTemporaryFile("plang", "o", Fd, TmpPath)) {
            diag(diag::err_cannot_create_temp_file, {EC.message()});
            if (OwnIr) llvm::sys::fs::remove(IrFile);
            return 1;
        }
        close(Fd);
        ObjFile = std::string(TmpPath);
        OwnObj  = true;
    }

    {
        std::vector<std::string> LLCArgs = {"-filetype=obj", "-relocation-model=pic", OOpt};
        // -g is not forwarded here; see the identical note on the assembly-
        // mode invocation above.
        if (!LLCTriple.empty())  { LLCArgs.push_back("--mtriple"); LLCArgs.push_back(LLCTriple); }
        LLCArgs.push_back("-o"); LLCArgs.push_back(ObjFile);
        LLCArgs.push_back(IrFile);
        Rc = runTool("llc", LLCArgs, V, DR);
    }
    if (OwnIr) llvm::sys::fs::remove(IrFile);
    if (Rc != 0) { if (OwnObj) llvm::sys::fs::remove(ObjFile); return Rc; }

    if (Opts.mode == OutputMode::Object) return 0;

    // Link, appending any extra object files from multi-file builds.
    Options LinkOpts = Opts;
    for (const auto &EO : ExtraObjs) LinkOpts.linkerArgs.push_back(EO);
    Rc = link(*this, LinkOpts, ObjFile, OutFile, findRuntimeLib(ExePath_), V, DR);
    if (OwnObj) llvm::sys::fs::remove(ObjFile);
    return Rc;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

int Driver::run(int Argc, char *Argv[]) {
    configureDiagnostics(Argc, Argv);

    Options Opts = parseArgs(Argc, Argv);
    // A malformed option has already been reported, and there may be more than
    // one of them; parsing carries on so that they all are.
    if (Diags_.hasErrors()) return 1;

    if (Opts.inputFile.empty()) {
        diag(diag::err_no_input_files);
        return 1;
    }
    // Both names and both lists come from Dialects.def, so the driver and the
    // front end cannot disagree about what -std= takes.
    if (!Opts.std.empty()) {
        const std::string Known = LangOptions::knownDialects();
        if (!LangOptions::parseDialect(Opts.std)) {
            diag(diag::err_unknown_dialect, {Opts.std, Known});
            return 1;
        }
        const std::string Impl = LangOptions::implementedDialects();
        if (!LangOptions::isImplementedDialect(Opts.std)) {
            diag(diag::err_dialect_not_implemented, {Opts.std, Impl});
            return 1;
        }
    }
    return compile(Opts);
}
