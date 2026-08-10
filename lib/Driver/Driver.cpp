/// plang driver — orchestrates the compilation pipeline.
///
/// Invokes the plang front end (by re-invoking itself with \c -pc1), llc for
/// code generation, and ld.lld for linking.  The front-end code lives in
/// libplang-frontend.so; the driver links against it so \c -pc1 mode can be
/// handled in-process without spawning a subprocess for trivial uses.

#include "plang/Driver/Driver.h"
#include "plang/Driver/Options.h"
#include "plang/Frontend/Frontend.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/DiagnosticPrinter.h"
#include "plang/Basic/Version.h"

#include <algorithm>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

using namespace plang;

// ---------------------------------------------------------------------------
// Version / target constants
// ---------------------------------------------------------------------------

static std::string hostTriple() {
    return llvm::sys::getDefaultTargetTriple();
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

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void Driver::configureDiagnostics(int Argc, char *Argv[]) {
    DiagnosticOptions DO;
    ColorDiagnostics  Color = ColorDiagnostics::Auto;

    for (int I = 1; I < Argc; ++I) {
        const std::string_view Arg = Argv[I];
        if (Arg == "-w")               DO.SuppressWarnings = true;
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
// UI helpers
// ---------------------------------------------------------------------------

void Driver::printVersion() {
    std::println("plang version {}\nTarget: {}\nThread model: {}\nInstalledDir: {}",
                 PLANG_VERSION_STRING, hostTriple(), threadModel(),
                 Driver().findInstallDir());
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

std::string Driver::defaultOutput(const std::string &InputFile, OutputMode Mode) {
    switch (Mode) {
        case OutputMode::Executable: return "a.out";
        case OutputMode::Assembly:   return stem(InputFile) + ".s";
        case OutputMode::Object:     return stem(InputFile) + ".o";
        case OutputMode::LLVMIr:     return stem(InputFile) + ".ll";
        case OutputMode::DumpAst:    return "";
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
        } else if (Arg.starts_with("-W") && Arg.size() > 2) {
            if (Arg.size() <= 3 || Arg[2] != 'l' || Arg[3] != ',')
                Opts.frontendArgs.push_back(Arg);
            else
                Opts.linkerArgs.push_back(Arg);

        } else if (Arg == "-Xlinker") {
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-Xlinker"}); continue; }
            Opts.linkerArgs.push_back("-Xlinker");
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
            diag(diag::warn_unrecognised_argument, {Arg});
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

/// Build the argv for a -pc1 front-end invocation.
static std::vector<std::string> makeFEArgs(const Options &Opts,
                                            const std::string &Out,
                                            bool DumpAst = false) {
    std::vector<std::string> Args = {"-pc1"};
    if (!Out.empty())          { Args.push_back("-o"); Args.push_back(Out); }
    if (DumpAst)                 Args.push_back("-dump-ast");
    Args.push_back(Opts.inputFile);
    if (!Opts.std.empty())     { Args.push_back("-std=" + Opts.std); }
    if (Opts.suppressWarnings)   Args.push_back("-w");
    if (Opts.warningsAsErrors)   Args.push_back("-Werror");
    if (!Opts.rangeChecks)       Args.push_back("-fno-range-checks");
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
static int linkWithLLD(Driver &D, const Options &Opts, const std::string &ObjFile,
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

    llvm::Triple T(Opts.target.empty() ? hostTriple()
                                       : llvm::Triple::normalize(Opts.target));
    std::string Emul = linkerEmulation(T);
    std::string DynL = dynamicLinker(T);
    std::string Arch = T.getArchName().str();

    std::string OOpt = "-O" + std::to_string(Opts.optLevel);
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

int Driver::compile(const Options &Opts) {
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

    // Multi-file mode: compile each extra input file to a .o first.
    // The extra files' directories are added to the module search path so PMI
    // files written alongside them are automatically found.
    std::vector<std::string> ExtraObjs;
    for (const auto &ExtraFile : Opts.extraInputFiles) {
        if (!llvm::sys::fs::exists(ExtraFile)) {
            diag(diag::err_file_not_found, {ExtraFile});
            return 1;
        }
        Options ExtraOpts = Opts;
        ExtraOpts.inputFile       = ExtraFile;
        ExtraOpts.mode            = OutputMode::Object;
        ExtraOpts.outputFile      = stem(ExtraFile) + ".o";
        ExtraOpts.extraInputFiles.clear(); // avoid recursion
        // Add the extra file's directory so its PMI files are discoverable.
        {
            auto Slash = ExtraFile.rfind('/');
            std::string Dir = (Slash == std::string::npos) ? "." : ExtraFile.substr(0, Slash);
            ExtraOpts.modulePaths.push_back(Dir);
        }
        if (compile(ExtraOpts) != 0) return 1;
        ExtraObjs.push_back(ExtraOpts.outputFile);
    }

    // DumpAst mode: front end only.
    if (Opts.mode == OutputMode::DumpAst)
        return runTool(Self, makeFEArgs(Opts, OutFile, /*DumpAst=*/true), V, DR);

    // LLVMIr mode: front end only.
    if (Opts.mode == OutputMode::LLVMIr)
        return runTool(Self, makeFEArgs(Opts, OutFile), V, DR);

    // Choose IR file.
    std::string IrFile;
    bool OwnIr = false;

    if (Opts.saveTemps) {
        IrFile = stem(Opts.inputFile) + ".ll";
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
    for (const auto &ExtraFile : Opts.extraInputFiles) {
        auto Slash = ExtraFile.rfind('/');
        std::string Dir = (Slash == std::string::npos) ? "." : ExtraFile.substr(0, Slash);
        // Avoid duplicates.
        bool Already = false;
        for (const auto &P : MainOpts.modulePaths) if (P == Dir) { Already = true; break; }
        if (!Already) MainOpts.modulePaths.push_back(Dir);
    }
    int Rc = runTool(Self, makeFEArgs(MainOpts, IrFile), V, DR);
    if (Rc != 0) {
        if (OwnIr) llvm::sys::fs::remove(IrFile);
        return Rc;
    }

    // Assembly mode: IR → .s.
    if (Opts.mode == OutputMode::Assembly) {
        std::vector<std::string> LLCArgs = {OOpt};
        if (Opts.debug)              LLCArgs.push_back("-g");
        if (!Opts.target.empty())  { LLCArgs.push_back("--mtriple"); LLCArgs.push_back(Opts.target); }
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
        if (Opts.debug)             LLCArgs.push_back("-g");
        if (!Opts.target.empty()) { LLCArgs.push_back("--mtriple"); LLCArgs.push_back(Opts.target); }
        LLCArgs.push_back("-o"); LLCArgs.push_back(ObjFile);
        LLCArgs.push_back(IrFile);
        Rc = runTool("llc", LLCArgs, V, DR);
    }
    if (OwnIr) llvm::sys::fs::remove(IrFile);
    if (Rc != 0) { if (OwnObj) llvm::sys::fs::remove(ObjFile); return Rc; }

    if (Opts.mode == OutputMode::Object) return 0;

    // Link with ld.lld, appending any extra object files from multi-file builds.
    Options LinkOpts = Opts;
    for (const auto &EO : ExtraObjs) LinkOpts.linkerArgs.push_back(EO);
    Rc = linkWithLLD(*this, LinkOpts, ObjFile, OutFile, findRuntimeLib(ExePath_), V, DR);
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
    if (!Opts.std.empty()) {
        static constexpr std::string_view Known[] =
            {"iso7185", "iso10206", "turbo", "delphi", "fpc"};
        bool IsKnown = false;
        for (auto D : Known) if (Opts.std == D) { IsKnown = true; break; }
        if (!IsKnown) {
            diag(diag::err_unknown_dialect,
                 {Opts.std, "iso7185, iso10206, turbo, delphi, fpc"});
            return 1;
        }
        static constexpr std::string_view Implemented[] = {"iso7185", "iso10206"};
        bool IsImplemented = false;
        for (auto D : Implemented) if (Opts.std == D) { IsImplemented = true; break; }
        if (!IsImplemented) {
            diag(diag::err_dialect_not_implemented,
                 {Opts.std, "iso7185, iso10206"});
            return 1;
        }
    }
    return compile(Opts);
}
