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
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/UnitSearchPath.h"
#include "plang/Basic/Version.h"

#include <algorithm>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
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

/// Deletes a temp file this process created for its own internal use (an
/// intermediate .ll/.o/probe-output file, never a user-requested -o target)
/// and un-registers it from RemoveFileOnSignal.
///
/// Every such temp file is registered with RemoveFileOnSignal right after
/// creation (issue #278: a driver killed by SIGINT/SIGTERM mid-compile used
/// to leave it behind -- there was no signal handling at all, only these
/// same explicit removes on the normal-return paths, which a signal does
/// not take).  Un-registering here, rather than leaving the registration
/// until process exit, keeps a signal arriving later in the same run (e.g.
/// while linking) from trying to remove a path already gone.
static void removeOwnTemp(llvm::StringRef Path) {
    llvm::sys::DontRemoveFileOnSignal(Path);
    llvm::sys::fs::remove(Path);
}

/// Resolves \p Path to a canonical, absolute form for same-file comparison
/// (issue #148: -o naming the same file as an input, just spelled
/// differently -- "./foo.pas" vs. "foo.pas").
///
/// real_path resolves symlinks too, but only answers for a path that already
/// exists -- which an -o target usually does not, since the whole point of
/// -o is to create or replace it.  When Path itself does not resolve, fall
/// back to resolving its parent directory (which normally does exist) and
/// reattaching the leaf name, so a same-directory-different-spelling case is
/// still caught without requiring the output file to pre-exist.
static std::string resolvePath(const std::string &Path) {
    llvm::SmallString<256> Real;
    if (!llvm::sys::fs::real_path(Path, Real, /*expand_tilde=*/false))
        return std::string(Real);

    llvm::SmallString<256> Abs(Path);
    llvm::sys::fs::make_absolute(Abs);
    const llvm::StringRef Leaf = llvm::sys::path::filename(Abs);
    llvm::SmallString<256> Dir(llvm::sys::path::parent_path(Abs));

    llvm::SmallString<256> RealDir;
    if (!Dir.empty() &&
        !llvm::sys::fs::real_path(Dir, RealDir, /*expand_tilde=*/false)) {
        llvm::sys::path::append(RealDir, Leaf);
        return std::string(RealDir);
    }

    // Neither the path nor its parent directory resolves (also missing) --
    // last resort is a purely lexical normalization.
    llvm::sys::path::remove_dots(Abs, /*remove_dot_dot=*/true);
    return std::string(Abs);
}

/// True if \p A and \p B name the same file once resolved, regardless of how
/// differently each is spelled on the command line.
static bool sameResolvedFile(const std::string &A, const std::string &B) {
    return !A.empty() && !B.empty() && resolvePath(A) == resolvePath(B);
}

// ---------------------------------------------------------------------------
// Auto-linking a used unit's own shipped object file
// ---------------------------------------------------------------------------
//
// Turbo Tier 4, Cluster C item 5: this is what lets `plang -std=turbo
// hello.pas -o hello` -- no -I, no naming crt.o by hand -- actually LINK
// when hello.pas says `uses Crt;`, matching what this item's own brief
// asked for ("real Turbo programs can uses Crt; with no flags").  Before
// this, separate compilation only worked with the resulting .o named
// explicitly on the command line (every existing Driver/Turbo lit test that
// links a separately-compiled unit does exactly that) -- Sema resolves a
// `uses` clause's TYPE information from a .tui/.pas on the search path
// today, but nothing carried that same resolution forward to the DRIVER's
// own link step, which runs the front end as a separate subprocess (-pc1)
// with no channel back for "here is what got used and where its object
// lives". Sema's own resolution is not reused here for exactly that
// reason: it lives inside that subprocess. What follows is deliberately a
// second, independent, best-effort resolution, run by the Driver itself,
// against the same three-tier unitSearchPaths() Sema's own last resort
// consults -- good enough to make Crt (and any future shipped unit
// following its own convention) auto-link, without the deeper "have the
// front end report back what it resolved" plumbing project.

/// A crude but sufficient scan for the identifiers named in \p Path's own
/// `uses` clause(s): finds the keyword "uses" and collects the comma-
/// separated identifiers up to the next non-identifier, non-comma,
/// non-whitespace character (in practice always ';', which is all this
/// grammar ever allows there). Deliberately whole-file, not scoped to only
/// the interface/implementation/program heading's own uses clause(s) --
/// scanning the WHOLE file for "uses" catches every one of them (a unit may
/// have both an interface and an implementation uses clause) at the cost of
/// a false positive only if the identifier "uses" appears somewhere else
/// entirely, which Pascal's own grammar never asks for. This is text
/// scanning, not real parsing -- unlike Sema's own loadUnitInterfaceExports,
/// it does not know about comments or string literals, so "uses" spelled
/// inside either of those (vanishingly unlikely in a Pascal source file)
/// would be misread; the cost of that is nothing worse than an extra
/// unresolvable name that findShippedUnitObject below silently ignores.
static std::vector<std::string> scanUsesClauseUnitNames(const std::string &Path) {
    std::vector<std::string> Names;
    auto BufOrErr = llvm::MemoryBuffer::getFile(Path);
    if (!BufOrErr) return Names;
    const llvm::StringRef Text = (*BufOrErr)->getBuffer();
    const size_t N = Text.size();
    auto isIdentChar = [](char C) {
        return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
               (C >= '0' && C <= '9') || C == '_';
    };
    auto isSpace = [](char C) {
        return C == ' ' || C == '\t' || C == '\r' || C == '\n';
    };
    for (size_t I = 0; I < N; ) {
        if (!isIdentChar(Text[I])) { ++I; continue; }
        const size_t Start = I;
        while (I < N && isIdentChar(Text[I])) ++I;
        if (!llvm::StringRef(Text.data() + Start, I - Start).equals_insensitive("uses")) continue;
        size_t J = I;
        for (;;) {
            while (J < N && isSpace(Text[J])) ++J;
            if (J >= N || !isIdentChar(Text[J])) break;
            const size_t IdStart = J;
            while (J < N && isIdentChar(Text[J])) ++J;
            Names.emplace_back(Text.data() + IdStart, J - IdStart);
            while (J < N && isSpace(Text[J])) ++J;
            if (J < N && Text[J] == ',') { ++J; continue; }
            break;
        }
        I = J;
    }
    return Names;
}

/// Where \p UnitName's own interface file (.tui, or failing that .pas) is
/// found, searching Sema::loadUnitInterfaceExports's identical three tiers
/// (ModulePaths/-I, then ".", then unitSearchPaths()) IN THAT ORDER, trying
/// both extensions in each directory before moving on to the next one --
/// see loadUnitInterfaceExports's own comment (issue #700) on why a single
/// per-directory pass, not two separate all-.tui-then-all-.pas passes, is
/// what actually gives a more specific directory priority over a less
/// specific one.  Returns the resolved interface file's own path and its
/// containing directory; both empty if UnitName is not found anywhere.
///
/// Issue #708: this is the SAME directory the front end's own Sema will
/// resolve UnitName's interface from (case sensitivity included: exact
/// lowercase match for .tui, since a .tui is always machine-written
/// lowercase; case-insensitive scan for .pas, since a hand-written unit's
/// source is not) -- deliberately duplicated rather than shared code,
/// because the driver runs the front end as a separate subprocess (-pc1)
/// with no channel back for "here is what got resolved and where", but kept
/// walking the identical tiers in the identical order so the two can never
/// disagree about which directory a given unit's artifacts live in.
struct ResolvedUnitInterface {
    std::string Path; // full path to the .tui or .pas; "" if not found
    std::string Dir;  // its containing directory; "" if not found
};
static ResolvedUnitInterface findUnitInterface(const std::string &UnitName,
                                                const std::vector<std::string> &ModulePaths,
                                                const std::string &InstallDir) {
    const std::string Key = plang::toLower(UnitName);
    std::vector<std::string> Dirs = ModulePaths;
    Dirs.push_back(".");
    for (const auto &P : plang::unitSearchPaths(InstallDir)) Dirs.push_back(P);
    for (const auto &Dir : Dirs) {
        const std::string TuiPath = Dir + "/" + Key + ".tui";
        if (isRegFile(TuiPath)) return {TuiPath, Dir};
        const std::string FastPas = Dir + "/" + Key + ".pas";
        if (isRegFile(FastPas)) return {FastPas, Dir};
        std::error_code EC;
        for (llvm::sys::fs::directory_iterator It(Dir, EC), End; It != End && !EC;
             It.increment(EC)) {
            const llvm::SmallString<64> Stem = llvm::sys::path::filename(It->path());
            if (llvm::StringRef(Stem).equals_insensitive(Key + ".pas"))
                return {std::string(It->path()), Dir};
        }
    }
    return {"", ""};
}

/// If \p UnitName has a shipped, already-compiled object file next to
/// wherever findUnitInterface (above) resolved its own .tui/.pas, returns
/// its path; "" if either the interface or the object was not found.
/// Resolving the object relative to the SAME directory the interface came
/// from -- rather than independently re-searching the whole tier list for a
/// same-named .o, which is what let issue #708 happen (a stale d1/foo.o
/// beating a newer d2/foo.tui's own d2/foo.o just because d1 sorted first)
/// -- is what keeps this coherent with Sema's own resolution: the object
/// linked in is always the one that sits beside the interface the program
/// was actually type-checked against.  The object file's own name is a
/// fixed convention this item establishes for every shipped unit, mirroring
/// the .tui's own: lowercase(UnitName) + ".o", written next to a lowercase
/// .tui the identical way share/plang/units/Crt.pas's own CMake rule
/// (top-level CMakeLists.txt) writes crt.o beside crt.tui.  The exact
/// lowercase name is tried first (cheap, and the only name the shipped RTL's
/// own CMake rule and every existing lowercase-unit lit test ever produce).
///
/// Issue #746: `plang -c` does NOT lowercase its default .o name the way
/// writeTUIFile lowercases the .tui -- Driver::defaultOutput's stem() keeps
/// the SOURCE FILE's own case, so `plang -c MathUtils.pas` writes
/// `MathUtils.o` (capital M) beside the still-lowercase `mathutils.tui`.
/// Since almost every hand-written unit is normally-capitalized, requiring
/// an exact lowercase match alone made this auto-linking feature (the
/// entire point of issue #705) silently fail for essentially all real code.
/// So when the exact lowercase name is not found, fall back to a case-
/// insensitive scan of the SAME directory (mirroring findUnitInterface's
/// own case-insensitive .pas fallback just above) for a same-named .o
/// however it was actually cased when compiled.  A hand-written unit's own
/// separately-compiled .o that does not even share the unit's name (a
/// differently-NAMED, not just differently-cased, object file) is still
/// exactly what this project's existing "name the .o explicitly on the
/// command line" workflow remains for.
static std::string findShippedUnitObject(const ResolvedUnitInterface &Resolved,
                                          const std::string &UnitName) {
    if (Resolved.Dir.empty()) return "";
    const std::string Key = plang::toLower(UnitName);
    const std::string ObjPath = Resolved.Dir + "/" + Key + ".o";
    if (isRegFile(ObjPath)) return ObjPath;

    const std::string WantName = Key + ".o";
    std::error_code EC;
    for (llvm::sys::fs::directory_iterator It(Resolved.Dir, EC), End; It != End && !EC;
         It.increment(EC)) {
        const llvm::SmallString<64> Stem = llvm::sys::path::filename(It->path());
        if (llvm::StringRef(Stem).equals_insensitive(WantName))
            return std::string(It->path());
    }
    return "";
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
    llvm::sys::RemoveFileOnSignal(OutPath);

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
    removeOwnTemp(OutPath);
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

// The shared runtime's own on-disk name: runtime/CMakeLists.txt gives
// plang_runtime_shared OUTPUT_NAME "plang" with no VERSION/SOVERSION, so it
// builds as an unversioned libplang.so on ELF hosts. CMake's own default
// MACOSX_RPATH behavior (CMP0042 NEW, the default since CMake 3.14, well
// under this project's cmake_minimum_required(3.16)) gives a Darwin shared
// library an "@rpath/<name>"-relative LC_ID_DYLIB automatically, so
// linkDarwin's own -dynamic path (below) only has to hand ld a matching
// -rpath, the same shape as the ELF path.
#if defined(__APPLE__)
static constexpr const char *SharedRuntimeLibFile = "libplang.dylib";
#else
static constexpr const char *SharedRuntimeLibFile = "libplang.so";
#endif

/// What findRuntimeLib found, in the shape each of its two callers needs:
///  - A static (or sanitized) link embeds the archive's own path directly as
///    an argv entry (StaticPath).
///  - A dynamic link instead needs -L<dir> -lplang plus a matching -rpath
///    <dir> so the resulting binary runs without LD_LIBRARY_PATH/
///    DYLD_LIBRARY_PATH (DynamicLibDir) -- see resolveRuntimeLib's own
///    comment for why a bare path does not work here the way it does for a
///    static archive.
struct RuntimeLibResult {
    std::string StaticPath;
    std::string DynamicLibDir;

    bool found() const { return !StaticPath.empty() || !DynamicLibDir.empty(); }
};

/// Locates the ordinary plang runtime (\p Dynamic selects libplang.so/
/// .dylib over libplang.a) or, when \p Sanitized is true (-sanitize-runtime,
/// issue #190 part B option 2), libplang_sanitized.a -- the ASan/UBSan-
/// instrumented plang_runtime_sanitized variant runtime/CMakeLists.txt only
/// builds when -DPLANG_ENABLE_RUNTIME_SANITIZER_TESTS=ON was given at CMake
/// configure time (\p Dynamic is ignored when \p Sanitized is set: that
/// variant is test infrastructure only and always static).
///
/// Checks PREFIX/<CMAKE_INSTALL_LIBDIR> at run time first (the installed
/// layout -- PLANG_INSTALL_LIBDIR, baked in at configure time from the exact
/// same CMAKE_INSTALL_LIBDIR value lib/CMakeLists.txt's own install() rules
/// use, rather than a hardcoded "lib": GNUInstallDirs resolves that to
/// "lib64" on Fedora, openSUSE, and other 64-bit multi-lib distros, and a
/// hardcoded "lib" here silently missed the runtime on exactly those
/// systems -- issue #805), then falls back to the baked-in build-directory
/// path. Returns a not-found() result if the file was not found either
/// place; the sanitized caller turns that into
/// err_sanitized_runtime_not_built, and the ordinary caller into
/// err_runtime_not_found (both in resolveRuntimeLib), rather than pressing
/// on into a confusing linker failure.
static RuntimeLibResult findRuntimeLib(const std::string &ExePath,
                                        bool Sanitized, bool Dynamic) {
    RuntimeLibResult Result;
    const char *LibFile = Sanitized ? "libplang_sanitized.a"
                          : Dynamic ? SharedRuntimeLibFile
                                    : "libplang.a";

    // Tries LibFile inside Dir; records it into Result in whichever shape
    // the caller wants and returns whether it was found.
    auto tryDir = [&](const llvm::Twine &Dir) -> bool {
        llvm::SmallString<256> Candidate(Dir.str());
        llvm::sys::path::append(Candidate, LibFile);
        if (!isRegFile(Candidate)) return false;
        if (Dynamic && !Sanitized)
            Result.DynamicLibDir = std::string(Dir.str());
        else
            Result.StaticPath = std::string(Candidate);
        return true;
    };

    if (!ExePath.empty()) {
        llvm::SmallString<256> BinDir(ExePath);
        llvm::sys::path::remove_filename(BinDir);
        llvm::SmallString<256> Prefix(BinDir);
        llvm::sys::path::remove_filename(Prefix);
        llvm::SmallString<256> LibDir(Prefix);
#ifdef PLANG_INSTALL_LIBDIR
        llvm::sys::path::append(LibDir, PLANG_INSTALL_LIBDIR);
#else
        llvm::sys::path::append(LibDir, "lib");
#endif
        if (tryDir(LibDir)) return Result;
    }
#ifdef PLANG_RUNTIME_DIR
    if (tryDir(PLANG_RUNTIME_DIR)) return Result;
#endif
    return Result;
}

// ---------------------------------------------------------------------------
// Version-numbered directory sorting (shared by the GCC and Darwin toolchain
// probes below, both of which pick the newest of several installed versions)
// ---------------------------------------------------------------------------

bool plang::versionDirLess(std::string_view A, std::string_view B) {
    llvm::VersionTuple VA, VB;
    // tryParse() returns true on FAILURE (llvm::VersionTuple convention).
    const bool AOk = !VA.tryParse(A);
    const bool BOk = !VB.tryParse(B);
    if (AOk && BOk) return VA < VB;
    if (AOk) return false; // A is a real version, B is not: A sorts after B.
    if (BOk) return true;  // B is a real version, A is not: A sorts before B.
    return A < B;          // Neither parses: fall back to a stable order.
}

// ---------------------------------------------------------------------------
// GCC installation detection (for ld.lld CRT/library paths)
// ---------------------------------------------------------------------------

struct GCCInstall {
    std::string Dir;    ///< versioned GCC lib dir
    std::string LibDir; ///< system lib dir containing Scrt1.o
};

/// Direct children of \p Parent, sorted ascending by versionDirLess so that
/// callers who want the newest installed version can walk the result
/// back-to-front (see detectGCC and builtinsUnder below).
static std::vector<std::string> subdirs(const std::string &Parent) {
    std::vector<std::string> Out;
    std::error_code EC;
    for (llvm::sys::fs::directory_iterator It(Parent, EC), End;
         !EC && It != End; It.increment(EC)) {
        llvm::StringRef Name = llvm::sys::path::filename(It->path());
        if (!Name.empty() && Name[0] != '.')
            Out.push_back(std::string(Name));
    }
    std::sort(Out.begin(), Out.end(), versionDirLess);
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
        case OutputMode::DumpVmt:       return "";
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
        // Args carries the input file straight from argv (see makeFEArgs and
        // the Opts.inputFile push below): -v/-### echo it unsanitized
        // otherwise, the same terminal-escape/log-injection hole a raw
        // filename opens in a diagnostic's "file:line:col:" prefix -- see
        // DiagnosticPrinter::printHeadline.  Each argument is control-char-
        // escaped first (issue #281) and *then* quoted the same way clang's
        // own -### output is (llvm::sys::printArg, which always wraps its
        // argument in quotes and backslash-escapes any embedded '"' or '\\')
        // -- issue #286.  The plain space-joined line this replaces could
        // not tell a two-word argument from two arguments once printed, so
        // it could be neither read correctly nor pasted back into a shell;
        // escaping control bytes first means printArg's own backslash-
        // escaping of the '\' that introduces each \xHH sequence keeps the
        // pasted-back form round-tripping safely too.
        std::string Line;
        llvm::raw_string_ostream LineOS(Line);
        llvm::sys::printArg(LineOS, escapeControlChars(Prog), /*Quote=*/true);
        for (const auto &A : Args) {
            LineOS << ' ';
            llvm::sys::printArg(LineOS, escapeControlChars(A), /*Quote=*/true);
        }
        std::cerr << Line << '\n';
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

Driver::ParseResult Driver::parseArgs(int Argc, char *Argv[]) {
    ParseResult PR;
    Options &Opts = PR.Opts;
    bool HasInput = false;

    for (int I = 1; I < Argc; ++I) {
        std::string Arg = Argv[I];

        if (Arg == "--version") {
            printVersion(); PR.EarlyExitCode = 0; return PR;
        } else if (Arg == "-dumpversion") {
            std::cout << PLANG_VERSION_STRING << "\n"; PR.EarlyExitCode = 0; return PR;
        } else if (Arg == "-dumpmachine") {
            std::cout << hostTriple() << "\n"; PR.EarlyExitCode = 0; return PR;
        } else if (Arg == "-h" || Arg == "--help") {
            usage(); PR.EarlyExitCode = 0; return PR;
        } else if (Arg == "--help-warnings") {
            std::println("Warnings, all enabled by default.  Turn one off with");
            std::println("-Wno-<name>, or all of them with -w.\n");
            forEachWarningName([](const std::string &N) {
                std::println("  -Wno-{}", N);
            });
            PR.EarlyExitCode = 0; return PR;

        } else if (Arg == "-###") {
            Opts.dryRun = true;
        } else if (Arg == "-v") {
            Opts.verbose = true;
        } else if (Arg == "-save-temps") {
            Opts.saveTemps = true;
        } else if (Arg == "-sanitize-runtime") {
            Opts.sanitizeRuntime = true;
        } else if (Arg == "-static") {
            Opts.linkRuntimeStatic = true;
        } else if (Arg == "-dynamic") {
            Opts.linkRuntimeStatic = false;

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
        } else if (Arg == "-dump-vmt") {
            Opts.mode = OutputMode::DumpVmt;
        } else if (Arg.size() > 2 && Arg[0] == '-' && Arg[1] == 'o') {
            // Joined form (issue #244): "-ojoined.o".  Options.def has always
            // declared -o JoinedOrSeparate, but this hardcoded fast path used
            // to implement only the separate half of that, so a joined -o
            // fell through to the generic Options.def-driven fallback further
            // down, which (since -o is a "Both" option) forwarded the whole,
            // still-prefixed string to the front end as an opaque argument
            // rather than recognizing it as -o's own value -- and the front
            // end's own parser had the identical gap, so it rejected the
            // forwarded string too.  Nothing glued on can be empty (that is
            // the separate form, handled below), so there is no empty-value
            // case to reject here.
            Opts.outputFile = Arg.substr(2);
        } else if (Arg == "-o") {
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-o"}); continue; }
            // Issue #286: an explicitly empty value ("-o ''", as a shell
            // passes one through from an empty or unset variable) used to be
            // accepted -- Opts.outputFile uses "" as its own sentinel for "no
            // -o was given at all" (see its doc comment in Driver.h), so an
            // empty -o was indistinguishable from no -o by the time anything
            // downstream looked at it, and silently fell back to the default
            // output name instead of reporting the mistake.  This is the only
            // point that can still tell the two cases apart.
            const std::string Val = Argv[++I];
            if (Val.empty()) { diag(diag::err_empty_output_filename); continue; }
            Opts.outputFile = Val;

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
        } else if (Arg == "-L") {
            // Separate form (issue #245): "-L dir".  Options.def used to
            // declare -L Joined-only, matching the gap here -- a standalone
            // "-L" matched no case in this chain (the generic Options.def
            // fallback below does not apply either: -L is Driver-only, so it
            // is never forwarded to the front end, the only thing that
            // fallback does) and fell all the way to the unrecognized-
            // argument catch-all, leaving its value to be picked up next as
            // if it were an ordinary input file.  Reassembled into the same
            // joined spelling linkerArgs already holds the glued form in,
            // rather than as two separate argv entries, since nothing else
            // in this file has needed the two-entries shape ld.lld also
            // accepts.
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-L"}); continue; }
            Opts.linkerArgs.push_back("-L" + std::string(Argv[++I]));
        } else if (Arg.size() > 2 && Arg[0] == '-' && Arg[1] == 'l') {
            Opts.linkerArgs.push_back(Arg);
        } else if (Arg == "-l") {
            // Separate form (issue #245): "-l lib" -- see the -L arm just
            // above, which has the same shape for the same reason.
            if (I + 1 >= Argc) { diag(diag::err_arg_requires_value, {"-l"}); continue; }
            Opts.linkerArgs.push_back("-l" + std::string(Argv[++I]));

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
    return PR;
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
/// "-dump-parse-tree", "-dump-vmt") that stop the pipeline before code
/// generation.
static std::vector<std::string> makeFEArgs(const Options &Opts,
                                            const std::string &Out,
                                            const char *DumpFlag = nullptr) {
    std::vector<std::string> Args = {"-pc1"};
    if (!Out.empty())          { Args.push_back("-o"); Args.push_back(Out); }
    if (DumpFlag)                Args.push_back(DumpFlag);
    Args.push_back(Opts.inputFile);
    if (!Opts.std.empty())     { Args.push_back("-std=" + Opts.std); }
    if (!Opts.target.empty())  { Args.push_back("--target=" + Opts.target); }
    if (Opts.suppressWarnings)   Args.push_back("-w");
    if (Opts.warningsAsErrors)   Args.push_back("-Werror");
    // Forwarded explicitly either way -- not just when checks are off, the
    // way this used to read -- so that -pc1 is never left to guess.  Its own
    // default (LangOptions.h's RangeChecks, computed the same way once Std
    // is known) is now dialect-aware, and leaving this "say nothing when
    // Opts.rangeChecks looks true" would make an explicit -frange-checks
    // under -std=turbo indistinguishable from no flag at all, silently
    // dropping the user's request to turn checks back on where Turbo starts
    // with them off.
    {
        const bool IsTurbo = LangOptions::parseDialect(Opts.std)
                              == LangOptions::Standard::Turbo;
        Args.push_back(Opts.rangeChecks.value_or(!IsTurbo)
                            ? "-frange-checks" : "-fno-range-checks");
    }
    if (!Opts.nilChecks)         Args.push_back("-fno-nil-checks");
    // The front end runs the LLVM pipeline; llc gets the same level separately
    // for instruction selection.
    Args.push_back("-O" + std::to_string(Opts.optLevel));
    if (Opts.debug)              Args.push_back("-g");
    for (const auto &A : Opts.frontendArgs) Args.push_back(A);
    for (const auto &P : Opts.modulePaths) Args.push_back("-I" + P);
    return Args;
}

/// Appends whichever of RuntimeLib's StaticPath/DynamicLibDir was resolved to
/// \p Args, in the shape the target linker wants: a static (or sanitized)
/// link is just the archive's own path as a single argv entry, exactly where
/// any other link input would go; a dynamic link instead needs the triple
/// -L<dir> -lplang -rpath <dir> -- -L and -lplang so the linker itself can
/// resolve libplang.so/.dylib, and -rpath so the resulting binary can find it
/// again at run time without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH (issue #805's
/// own request: a user who had to pass -lplang by hand before these flags
/// existed got a working link but only because the *build* host happened to
/// have libplang.so on the linker's default search path -- nothing made the
/// resulting binary itself able to find it again at run time without that
/// same host's default runtime linker search path also including it).
static void appendRuntimeLinkArgs(const RuntimeLibResult &RuntimeLib,
                                  std::vector<std::string> &Args) {
    if (!RuntimeLib.StaticPath.empty()) {
        Args.push_back(RuntimeLib.StaticPath);
    } else if (!RuntimeLib.DynamicLibDir.empty()) {
        Args.push_back("-L" + RuntimeLib.DynamicLibDir);
        Args.push_back("-lplang");
        Args.push_back("-rpath");
        Args.push_back(RuntimeLib.DynamicLibDir);
    }
}

/// Link ObjFile into OutFile using ld.lld with detected GCC CRT paths.
///
/// Takes the Driver so that it can report through the same engine everything
/// else does, and run ld.lld through the same runTool.
static int linkELF(Driver &D, const Options &Opts, const std::string &ObjFile,
                   const std::string &OutFile,
                   const RuntimeLibResult &RuntimeLib,
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
    // ObjFile is empty in linker-only mode (no .pas input, just .o/.a files
    // forwarded straight to the linker via Opts.linkerArgs below); an empty
    // argument would otherwise be handed to ld.lld as a bogus input file.
    if (!ObjFile.empty()) Args.push_back(ObjFile);
    appendRuntimeLinkArgs(RuntimeLib, Args);
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
                      const RuntimeLibResult &RuntimeLib,
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
    // See the matching comment in linkELF: empty in linker-only mode.
    if (!ObjFile.empty()) Args.push_back(ObjFile);
    appendRuntimeLinkArgs(RuntimeLib, Args);
    for (const auto &A : Opts.linkerArgs) Args.push_back(A);
    Args.push_back("-lSystem");
    // Left out rather than reported when it is missing: it is only wanted by
    // programs that do complex arithmetic, and everything else links without.
    if (!TC.BuiltinsLib.empty()) Args.push_back(TC.BuiltinsLib);

    return D.runTool(TC.Linker, Args, Verbose, DryRun);
}

/// Link ObjFile into OutFile using clang as the whole link driver, for
/// -sanitize-runtime (issue #190 part B option 2) only.
///
/// linkELF/linkDarwin above invoke ld.lld/ld directly, with every CRT object
/// and system library they need found and named by hand -- there is no
/// comparable hand-written recipe to give an ASan/UBSan-instrumented link:
/// which compiler-rt archives to pull in, in what order, wrapped in
/// --whole-archive or not, each with its own --dynamic-list of the symbols
/// the sanitizer runtime intercepts, plus extra system libraries the runtime
/// itself needs (-lpthread/-lrt/-ldl/-lresolv on this project's ELF
/// targets) is a clang/compiler-rt-version- and platform-specific recipe
/// with no stable command-line contract of its own -- confirmed empirically
/// while wiring this in (`clang -fsanitize=address,undefined -### ...`
/// against this project's own pinned LLVM/Clang version shows a nine-
/// archive, three-dynamic-list recipe that differs across a plain `clang`
/// vs `clang++` invocation alone). Reimplementing that by hand here would
/// be exactly the kind of version-fragile guesswork this project's own
/// Darwin builtins-lib lookup (builtinsUnder, above) already tries to avoid
/// even for the single, much simpler -lclang_rt.osx.a case. Handing the
/// whole link to clang -fsanitize=address,undefined instead -- the same
/// flag pair plang_runtime_sanitized's own PUBLIC target_link_options
/// already puts on test/unittests/RuntimeSanitized's link (runtime/
/// CMakeLists.txt) -- makes clang assemble that recipe itself, the same way
/// it would for any other ASan/UBSan-instrumented C program.
///
/// clang, not clang++: plang_runtime_sanitized recompiles the same
/// PLANG_RUNTIME_SOURCES the ordinary runtime does, which (runtime/
/// plang_sys.cpp's own comment on why it avoids std::string) deliberately
/// never uses operator new/delete, exceptions or the STL specifically so
/// that linking it never needs libstdc++ -- confirmed empirically that
/// clang++ (unlike clang) always adds -lstdc++ plus a second,
/// C++-specific ASan archive even when every input is a plain .o/.a, purely
/// because it was invoked in C++ mode. clang keeps this link on the same
/// "no libstdc++ dependency" footing the ordinary linkELF/linkDarwin paths
/// are already on.
///
/// Opts.linkerArgs entries (-L/-l/-Wl,-split tokens/-Xlinker values/bare
/// .o or .a files, and any auto-linked shipped-unit objects) are each
/// individually re-wrapped in their own "-Xlinker" here rather than passed
/// through as bare argv entries: unlike ld.lld above, which takes every one
/// of them literally as its own argv, clang's own driver would otherwise
/// try to interpret a plain GNU-style linker flag (e.g. one that started
/// out as "-Wl,--defsym,x=y" and was already comma-split by parseArgs) as a
/// *compiler* flag it does not recognize, rather than forwarding it to the
/// linker untouched the way -Xlinker guarantees.
static int linkSanitized(Driver &D, const Options &Opts,
                         const std::string &ObjFile, const std::string &OutFile,
                         const RuntimeLibResult &RuntimeLib, bool Verbose, bool DryRun) {
    // Baked in at configure time from CMAKE_C_COMPILER (lib/Driver/
    // CMakeLists.txt), which PLANG_ENABLE_RUNTIME_SANITIZER_TESTS's own
    // configure-time check (top-level CMakeLists.txt) requires to be Clang
    // -- the same compiler plang_runtime_sanitized was itself built with,
    // rather than whatever "clang" first resolves to on PATH at run time,
    // which could be a different (and ABI-incompatible) sanitizer runtime
    // version. The plain "clang" fallback below is unreachable in a build
    // that actually has plang_runtime_sanitized to link (findRuntimeLib
    // already turns a missing sanitized archive into
    // err_sanitized_runtime_not_built before this function is ever called)
    // -- kept only so this still names a real program rather than an empty
    // string in the hypothetical case of a non-CMake build system defining
    // things differently.
#ifdef PLANG_SANITIZER_CLANG
    static constexpr char ClangProg[] = PLANG_SANITIZER_CLANG;
#else
    static constexpr char ClangProg[] = "clang";
#endif

    std::vector<std::string> Args;
    if (!Opts.target.empty()) Args.push_back("--target=" + Opts.target);
    Args.push_back("-fsanitize=address,undefined");
    // -fno-sanitize=vptr: matches plang_runtime_sanitized's own compile
    // flags (runtime/CMakeLists.txt) -- the runtime is plain C structs, no
    // RTTI, so UBSan's vptr check has nothing to check.
    Args.push_back("-fno-sanitize=vptr");
    Args.push_back("-o"); Args.push_back(OutFile);
    // See the matching comment in linkELF: empty in linker-only mode.
    if (!ObjFile.empty()) Args.push_back(ObjFile);
    for (const auto &A : Opts.linkerArgs) {
        Args.push_back("-Xlinker");
        Args.push_back(A);
    }
    // RuntimeLib LAST, after every other object file (ObjFile, and any
    // multi-file build's extra unit .o's forwarded through Opts.linkerArgs
    // just above) -- confirmed the hard way while wiring this in: a plain
    // GNU ld (unlike ld.lld, which linkELF above uses directly and tolerates
    // either order) resolves a static archive's members in one single left-
    // to-right pass, never reconsidering an archive already scanned once
    // the file after it needs a symbol only that archive provides. Placing
    // it before a multi-file build's own extra unit object (e.g.
    // vartypesunit.o calling plang_str_init from its own unit initializer)
    // left that reference undefined -- clang has no fixed default linker of
    // its own to rely on here the way ld.lld's own direct invocation does,
    // so this has to be right for whichever one clang picks.
    //
    // Always RuntimeLib.StaticPath, never DynamicLibDir: the sanitized
    // runtime (findRuntimeLib's Sanitized case) is test infrastructure only
    // and always statically linked regardless of -static/-dynamic -- see
    // findRuntimeLib's own comment.
    if (!RuntimeLib.StaticPath.empty()) Args.push_back(RuntimeLib.StaticPath);

    return D.runTool(ClangProg, Args, Verbose, DryRun);
}

/// Link ObjFile into OutFile with whichever linker the target calls for.
static int link(Driver &D, const Options &Opts, const std::string &ObjFile,
                const std::string &OutFile, const RuntimeLibResult &RuntimeLib,
                bool Verbose, bool DryRun) {
    if (Opts.sanitizeRuntime)
        return linkSanitized(D, Opts, ObjFile, OutFile, RuntimeLib, Verbose, DryRun);
    if (targetTriple(Opts).isOSDarwin())
        return linkDarwin(D, Opts, ObjFile, OutFile, RuntimeLib, Verbose, DryRun);
    return linkELF(D, Opts, ObjFile, OutFile, RuntimeLib, Verbose, DryRun);
}

/// Resolves the runtime library to link against -- plang_runtime_sanitized
/// when Opts.sanitizeRuntime (-sanitize-runtime) is set, the ordinary
/// plang_runtime/plang_runtime_shared otherwise, static or dynamic per
/// Opts.linkRuntimeStatic -- into \p RuntimeLib, and reports a diagnostic up
/// front (returning false) rather than pressing on into a confusing linker
/// failure when the needed variant was not found:
///
///  - err_sanitized_runtime_not_built when -sanitize-runtime was given but
///    this build was not configured with
///    -DPLANG_ENABLE_RUNTIME_SANITIZER_TESTS=ON (see findRuntimeLib's own
///    comment for why that leaves \p RuntimeLib not-found()).
///  - err_runtime_not_found (issue #805) when a real, ordinary Pascal-
///    sourced link needs the runtime and it is missing -- a genuinely
///    broken/incomplete install, or (for -dynamic) one that shipped only the
///    static archive. Gated on \p RequireRuntime, which every caller except
///    the linker-only-mode call site in Driver::compile() passes true:
///    linker-only mode (issue #611 -- a "plang a.o b.o -o out" invocation
///    with no .pas source at all) has no Pascal-sourced symbols that could
///    possibly need plang_runtime in the first place, so a missing runtime
///    there is not an error -- link() and its ELF/Darwin backends already
///    tolerate a not-found() RuntimeLib on their own for exactly that case
///    (see linkELF's "empty in linker-only mode" comment).
static bool resolveRuntimeLib(Driver &D, const Options &Opts,
                              const std::string &ExePath,
                              bool RequireRuntime,
                              RuntimeLibResult &RuntimeLib) {
    RuntimeLib = findRuntimeLib(ExePath, Opts.sanitizeRuntime,
                                 !Opts.linkRuntimeStatic);
    if (Opts.sanitizeRuntime && !RuntimeLib.found()) {
        D.diag(diag::err_sanitized_runtime_not_built);
        return false;
    }
    if (!Opts.sanitizeRuntime && RequireRuntime && !RuntimeLib.found()) {
        D.diag(diag::err_runtime_not_found);
        return false;
    }
    return true;
}

int Driver::compile(const Options &Opts, bool IsExtraFile) {
    const std::string OutFile = Opts.outputFile.empty()
        ? defaultOutput(Opts.inputFile, Opts.mode)
        : Opts.outputFile;

    // Linker-only mode: no .pas input, only .o/.a files (parseArgs routed
    // them into Opts.linkerArgs).  There is no source to run the front end
    // or assembler on -- go straight to the link step, matching the standard
    // "compile with -c, link separately" workflow every C toolchain supports.
    //
    // But only when Opts.mode actually links (Executable): under -c, -S,
    // -emit-llvm or a -dump-* mode there is nothing to compile (no .pas
    // input) and nothing to link either (issue #611) -- those modes stop
    // before the link step for a real .pas input, and a linker-only
    // invocation must not be the one combination that reaches link()
    // anyway.  Driver::run() has already warned (or, under -Werror, failed)
    // that every .o/.a on the command line goes unused in that case, so
    // there is nothing left to do here but report success with no output
    // produced -- matching gcc's own "plang -c foo.o" response.
    if (Opts.inputFile.empty()) {
        if (Opts.mode != OutputMode::Executable) return 0;
        RuntimeLibResult RuntimeLib;
        // RequireRuntime=false: linker-only mode (issue #611), no .pas
        // source at all, so there is nothing here that could need
        // plang_runtime -- see resolveRuntimeLib's own comment.
        if (!resolveRuntimeLib(*this, Opts, ExePath_, /*RequireRuntime=*/false,
                               RuntimeLib))
            return 1;
        return link(*this, Opts, /*ObjFile=*/"", OutFile,
                    RuntimeLib, Opts.verbose, Opts.dryRun);
    }

    const std::string Self = findSelf();
    const std::string OOpt = "-O" + std::to_string(Opts.optLevel);
    const bool V  = Opts.verbose;
    const bool DR = Opts.dryRun;

    // Check the input file before spawning the front end.
    if (!llvm::sys::fs::exists(Opts.inputFile)) {
        diag(diag::err_file_not_found, {Opts.inputFile});
        return 1;
    }
    if (llvm::sys::fs::is_directory(Opts.inputFile)) {
        diag(diag::err_is_a_directory, {Opts.inputFile});
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
    // The subset of ExtraObjs that are ours to delete once the link step
    // below (the only thing that reads them) is done with them: an extra
    // file's object is always just an intermediate, never a user-requested
    // artifact, so leaving it in the cwd is litter (issue #279) exactly like
    // the main file's own OwnObj temp is guarded against further down --
    // this is the same fix, just for N files instead of one.  Populated
    // at creation, not after a successful compile, so a *failed* extra
    // compile's already-created temp is cleaned up too; deleted through a
    // scope_exit rather than only after a successful link so that an early
    // return above the link step (a dump mode, or the main file's own
    // front end failing) does not skip the cleanup either.
    std::vector<std::string> ExtraObjTemps;
    llvm::scope_exit CleanupExtraObjTemps([&] {
        for (const auto &F : ExtraObjTemps) removeOwnTemp(F);
    });
    for (const auto &ExtraFile : Opts.extraInputFiles) {
        if (!llvm::sys::fs::exists(ExtraFile)) {
            diag(diag::err_file_not_found, {ExtraFile});
            return 1;
        }
        if (llvm::sys::fs::is_directory(ExtraFile)) {
            diag(diag::err_is_a_directory, {ExtraFile});
            return 1;
        }
        Options ExtraOpts = Opts;
        ExtraOpts.inputFile       = ExtraFile;
        ExtraOpts.mode            = OutputMode::Object;

        // Object mode (-c) never reaches the link step below that would
        // otherwise consume these as intermediates -- each one IS the
        // requested output, one real .o per source (issue #612).  Before
        // this fix every extra file's object was unconditionally treated as
        // a link-only temporary and deleted once compile() returned, even
        // though -c never linked it into anything: only the main file's own
        // "first.o" survived, and "second.o", "third.o", etc. were silently
        // discarded.  Given the same permanent, collision-proof naming
        // -save-temps already used for its own different reason (keeping a
        // human-visible intermediate around) so the two paths share one
        // naming scheme instead of two.
        const bool KeepExtraObj = Opts.saveTemps || Opts.mode == OutputMode::Object;
        if (KeepExtraObj) {
            // A visible, permanent artifact: flattenedStem, not stem, so two
            // extra files sharing a basename in different directories
            // (issue #20) do not both default to the same "foo.o" in the
            // cwd.  flattenedStem's '/'->'_' folding is itself not
            // injective, though -- "unitA/b_c.pas" and "unitA_b/c.pas" both
            // flatten to "unitA_b_c" -- so a second file landing on a name
            // an earlier one already claimed (checked against ExtraObjs,
            // which holds every prior extra file's own final name) gets a
            // numeric suffix instead of silently overwriting the first
            // file's object (issue #170).
            const std::string Base = flattenedStem(ExtraFile);
            std::string Name = Base + ".o";
            for (int N = 2; std::find(ExtraObjs.begin(), ExtraObjs.end(), Name) != ExtraObjs.end(); ++N)
                Name = Base + "~" + std::to_string(N) + ".o";
            ExtraOpts.outputFile = Name;
        } else {
            // Just an intermediate needed for the final link, not a
            // human-facing artifact: a real, OS-named unique temp file,
            // exactly like the main file's own OwnObj below.  This sidesteps
            // the flattenedStem collision above entirely -- the name comes
            // from the OS, not from folding the input path -- and keeps it
            // out of the cwd (issue #279).
            llvm::SmallString<128> TmpPath;
            int Fd;
            if (auto EC = llvm::sys::fs::createTemporaryFile("plang", "o", Fd, TmpPath)) {
                diag(diag::err_cannot_create_temp_file, {EC.message()});
                return 1;
            }
            close(Fd);
            llvm::sys::RemoveFileOnSignal(TmpPath);
            ExtraOpts.outputFile = std::string(TmpPath);
            ExtraObjTemps.push_back(ExtraOpts.outputFile);
        }

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

    // DumpAst/DumpTokens/DumpParseTree/DumpVmt modes: front end only.
    if (Opts.mode == OutputMode::DumpAst)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-ast"), V, DR);
    if (Opts.mode == OutputMode::DumpTokens)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-tokens"), V, DR);
    if (Opts.mode == OutputMode::DumpParseTree)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-parse-tree"), V, DR);
    if (Opts.mode == OutputMode::DumpVmt)
        return runTool(Self, makeFEArgs(Opts, OutFile, "-dump-vmt"), V, DR);

    // LLVMIr mode: front end only.
    if (Opts.mode == OutputMode::LLVMIr)
        return runTool(Self, makeFEArgs(Opts, OutFile), V, DR);

    // Choose IR file.
    std::string IrFile;
    bool OwnIr = false;

    if (Opts.saveTemps) {
        if (IsExtraFile) {
            // Derived from Opts.outputFile -- the .o name the calling loop
            // above already assigned this same extra file, disambiguated
            // against every sibling extra file it had already planned a
            // name for (issue #170) -- rather than recomputing
            // flattenedStem(Opts.inputFile) independently here. Two extra
            // files whose flattenedStem collides (e.g. "unitA/b_c.pas" and
            // "unitA_b/c.pas", both "unitA_b_c") would otherwise still
            // collide on their *.ll* even after that .o fix: this function
            // has no visibility into its siblings to detect the collision
            // itself, only the base name the caller already resolved it to.
            std::string Base = Opts.outputFile;
            if (llvm::StringRef(Base).ends_with(".o")) Base.resize(Base.size() - 2);
            IrFile = Base + ".ll";
        } else {
            // The main file's own naming is untouched -- stem(), as always
            // -- since it has no sibling to collide with.
            IrFile = stem(Opts.inputFile) + ".ll";
        }
    } else {
        llvm::SmallString<128> TmpPath;
        int Fd;
        if (auto EC = llvm::sys::fs::createTemporaryFile("plang", "ll", Fd, TmpPath)) {
            diag(diag::err_cannot_create_temp_file, {EC.message()});
            return 1;
        }
        close(Fd);
        llvm::sys::RemoveFileOnSignal(TmpPath);
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
        if (OwnIr) removeOwnTemp(IrFile);
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
        if (OwnIr) removeOwnTemp(IrFile);
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
            if (OwnIr) removeOwnTemp(IrFile);
            return 1;
        }
        close(Fd);
        llvm::sys::RemoveFileOnSignal(TmpPath);
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
    if (OwnIr) removeOwnTemp(IrFile);
    if (Rc != 0) { if (OwnObj) removeOwnTemp(ObjFile); return Rc; }

    if (Opts.mode == OutputMode::Object) return 0;

    // Turbo Tier 4, Cluster C item 5: auto-link every `uses`d unit (the
    // main file's own, and every extra file's) that has a shipped,
    // already-compiled object file waiting on the unit search path -- see
    // findShippedUnitObject's own comment.  Skipped for anything already
    // named explicitly, either as an extra input file or as a bare .o/.a
    // linker argument, so a user overriding a shipped unit's object (e.g.
    // testing a locally-rebuilt Crt.o) is never fighting this against their
    // own explicit choice.
    //
    // Issue #705: this has to be the TRANSITIVE closure, not just the units
    // named directly by the program's own sources. If unit A `uses` unit B,
    // a program that only names A still needs B's own object linked in for
    // anything B contributes that A's own interface re-exports (real `fpc`
    // resolves this transitively too) -- so the worklist below is seeded
    // from the program's own sources and then grown from each resolved
    // unit's own interface file's `uses` clause(s), the same crude-but-
    // sufficient scanUsesClauseUnitNames scan either way.
    {
        std::vector<std::string> Worklist;
        {
            std::vector<std::string> Sources{Opts.inputFile};
            for (const auto &E : Opts.extraInputFiles) Sources.push_back(E);
            for (const auto &Src : Sources)
                for (const auto &Unit : scanUsesClauseUnitNames(Src)) Worklist.push_back(Unit);
        }
        std::vector<std::string> SeenUnits;
        for (size_t I = 0; I < Worklist.size(); ++I) {
            // By value: Worklist itself grows below, which would invalidate
            // a reference into it.
            const std::string Unit = Worklist[I];
            const std::string Key = toLower(Unit);
            if (std::find(SeenUnits.begin(), SeenUnits.end(), Key) != SeenUnits.end())
                continue;
            SeenUnits.push_back(Key);

            const ResolvedUnitInterface Resolved =
                findUnitInterface(Unit, Opts.modulePaths, findInstallDir());
            if (Resolved.Dir.empty()) continue;

            for (const auto &Transitive : scanUsesClauseUnitNames(Resolved.Path))
                Worklist.push_back(Transitive);

            const std::string Obj = findShippedUnitObject(Resolved, Unit);
            if (Obj.empty()) continue;
            bool Already = false;
            for (const auto &EO : ExtraObjs)
                if (sameResolvedFile(EO, Obj)) { Already = true; break; }
            for (const auto &A : Opts.linkerArgs)
                if (sameResolvedFile(A, Obj)) { Already = true; break; }
            if (!Already) ExtraObjs.push_back(Obj);
        }
    }

    // Link, appending any extra object files from multi-file builds (and any
    // shipped units' own objects auto-linked just above).
    Options LinkOpts = Opts;
    for (const auto &EO : ExtraObjs) LinkOpts.linkerArgs.push_back(EO);
    RuntimeLibResult RuntimeLib;
    // RequireRuntime=true: this is a real .pas-sourced compile reaching its
    // own link step, which always needs the plang runtime -- unlike the
    // linker-only-mode call site above.
    if (!resolveRuntimeLib(*this, LinkOpts, ExePath_, /*RequireRuntime=*/true,
                           RuntimeLib)) {
        if (OwnObj) removeOwnTemp(ObjFile);
        return 1;
    }
    Rc = link(*this, LinkOpts, ObjFile, OutFile, RuntimeLib, V, DR);
    if (OwnObj) removeOwnTemp(ObjFile);
    return Rc;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

int Driver::run(int Argc, char *Argv[]) {
    configureDiagnostics(Argc, Argv);

    ParseResult PR = parseArgs(Argc, Argv);
    // --version, --help, -dumpversion, -dumpmachine, --help-warnings: already
    // printed everything they have to print from inside parseArgs itself:
    // nothing left to do but hand the exit code it settled on back to our own
    // caller (issue #174 -- this used to be a direct std::exit() call instead,
    // which reported the same code but never returned control, to us or to
    // anyone who might be calling Driver::run() as a library rather than
    // running the plang binary as a subprocess). Checked before hasErrors()
    // below, matching the std::exit() this replaced: an informational action
    // still wins over an error already reported for an earlier argument, e.g.
    // "plang -Werror -fbogus-option --version" still prints the version
    // banner and exits 0, exactly as it did before.
    if (PR.EarlyExitCode) return *PR.EarlyExitCode;
    Options Opts = std::move(PR.Opts);
    // A malformed option has already been reported, and there may be more than
    // one of them; parsing carries on so that they all are.
    if (Diags_.hasErrors()) return 1;

    // Truly nothing to do only when there is no .pas input *and* no
    // linker-only input (.o/.a) either; a linker-only invocation (e.g.
    // "plang hello.o -o hello_bin") skips straight to the link step below.
    if (Opts.inputFile.empty() && Opts.linkerArgs.empty()) {
        diag(diag::err_no_input_files);
        return 1;
    }

    // Refuse to let -o overwrite one of the inputs (issue #148): without this
    // check "plang hello.pas -o hello.pas" ran the whole pipeline and let the
    // final write step truncate the very source it had just parsed, silently
    // destroying it.  gcc and clang have refused this for exactly the same
    // reason since forever ("fatal error: input file ... is the same as
    // output file"); this matches their wording and their exit-before-doing-
    // anything behavior.  Computed the same way compile() computes the real
    // output name, so a defaulted name (e.g. the bare "a.out" a linker-only
    // invocation falls back to) is checked too, not just an explicit -o.
    {
        const std::string OutFile = Opts.outputFile.empty()
            ? defaultOutput(Opts.inputFile, Opts.mode)
            : Opts.outputFile;
        if (!OutFile.empty()) {
            std::vector<std::string> Inputs;
            if (!Opts.inputFile.empty()) Inputs.push_back(Opts.inputFile);
            for (const auto &F : Opts.extraInputFiles) Inputs.push_back(F);
            // Bare .o/.a filenames that parseArgs routed into linkerArgs --
            // as opposed to -l/-L/-Wl,-prefixed flags, which never name a
            // file plang itself reads or writes.  gcc applies this identical
            // check to them too ("gcc t.o -o t.o" fails the same way, verified
            // empirically), and an already-compiled object being fed to the
            // link step is no less worth protecting than a .pas source.
            for (const auto &A : Opts.linkerArgs) {
                if (A.empty() || A[0] == '-' || A.size() < 2) continue;
                const std::string_view Ext(A.data() + A.size() - 2, 2);
                if (Ext == ".o" || Ext == ".a") Inputs.push_back(A);
            }
            for (const auto &In : Inputs) {
                if (sameResolvedFile(In, OutFile)) {
                    diag(diag::err_input_output_same, {In});
                    return 1;
                }
            }
        }
    }

    // -c with multiple .pas inputs and an explicit -o (issue #612): -c now
    // writes one real object per source (see the extra-file compile loop in
    // compile()), so a single -o name has nowhere to put all of them.
    // Checked up front, before compiling anything, matching every other
    // check in this block.
    if (Opts.mode == OutputMode::Object && !Opts.extraInputFiles.empty() &&
        !Opts.outputFile.empty()) {
        diag(diag::err_output_with_multiple_compile_only_inputs);
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

    // -c/-S/-emit-llvm/-dump-* alongside a precompiled .o/.a (issue #277):
    // none of those modes ever reaches the link step where linkerArgs would
    // otherwise be consumed, so name what is about to be silently ignored.
    // Also fires for a linker-only invocation (no .pas input at all, e.g.
    // "plang -c foo.o -o result", issue #611): compile()'s linker-only
    // branch used to send that straight to link() regardless of Opts.mode,
    // which actually performed the forbidden link instead of leaving the
    // object unused -- now it mirrors every other non-linking mode and
    // does not link, so the object genuinely is unused here too. Only bare
    // .o/.a filenames are named -- the same subset the input/output-
    // collision check above already isolates -- since -l/-L/-Wl,/-Xlinker
    // are recognized linker flags in their own right, not files that look
    // like this one was meant to be consumed and was not; neither gcc nor
    // clang warns about those in -c mode either (verified empirically
    // against both).
    if (Opts.mode != OutputMode::Executable) {
        for (const auto &A : Opts.linkerArgs) {
            if (A.empty() || A[0] == '-' || A.size() < 2) continue;
            const std::string_view Ext(A.data() + A.size() - 2, 2);
            if (Ext == ".o" || Ext == ".a") diag(diag::warn_linker_input_unused, {A});
        }
        // -Werror turns the diagnostic just above into "error:", the same
        // way it does for warn_unrecognized_argument -- but unlike that one,
        // reported from inside parseArgs with a hasErrors() check right
        // after it returns, this one is reported well after that check
        // already ran, so it needs its own or -Werror would print "error:"
        // and then compile anyway.
        if (Diags_.hasErrors()) return 1;
    }

    return compile(Opts);
}
