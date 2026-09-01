#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"

#include <optional>
#include <string>
#include <vector>

namespace plang {

/// A library-shaped entry point into the same Scanner -> Parser -> Sema ->
/// Codegen pipeline frontendPC1Main (Frontend.h) drives from argv, for an
/// embedder that wants to compile in-process rather than by spawning
/// "plang -pc1 ..." as a subprocess and scraping its stdout/stderr/exit
/// code. compile() and frontendPC1Main share one internal implementation
/// (Frontend.cpp's own compileRequest()) -- frontendPC1Main is layered on
/// top of it, not the other way round, so the two can never disagree about
/// what a given LangOptions/source pair compiles to.
///
/// Two constraints an embedder has to design around, neither of which is
/// this API's own choice to change (issue #179's phase 3, deliberately not
/// attempted here):
///
///   - Locale selection (plang::selectLocale, MessageCatalog.cpp) is
///     process-global mutable state -- "plang is single-threaded and
///     resolves once per process", straight from that function's own
///     comment -- not a per-request setting. compile() itself never calls
///     selectLocale; every Diagnostic::Message it returns is formatted
///     against whatever locale the process last selected (English if
///     nothing ever did), the same catalog every other compile() call and
///     frontendPC1Main itself in the same process shares. An embedder
///     that needs a specific language has to call selectLocale() itself,
///     once, before compiling -- and one process cannot serve two
///     different languages to two concurrent compiles.
///
///   - A module or Turbo unit compiled through a CompilationRequest still
///     publishes its own .pmi/.tui interface file next to SourceName as a
///     side effect (see writePMIFiles/writeTUIFile in Frontend.cpp) --
///     even when Buffer is set and there was no real source file to begin
///     with. There is no flag to suppress this: an embedder that does not
///     want filesystem writes from an in-memory compile has to make sure
///     the source it hands in declares no module or unit interface.
struct CompilationRequest {
    /// Dialect, warning-as-feature-gate, and codegen options. Default-
    /// constructed means strict ISO 7185, matching LangOptions' own default.
    /// Unlike frontendPC1Main, nothing here derives Opts.UnitSearchPaths
    /// from an installation directory automatically -- a caller that wants
    /// the shipped RTL units importable has to add them itself (see
    /// plang::unitSearchPaths, UnitSearchPath.h, which is what
    /// frontendPC1Main itself calls to fill this in from argv[0]).
    LangOptions Opts;

    /// -w / -Werror / -Wno-<name> / -ferror-limit= policy for this compile.
    DiagnosticOptions DiagOpts;

    /// What diagnostics call this compile's source ("foo.pas", "<buffer>",
    /// ...). Also where, when Buffer below is unset, the source is actually
    /// read from -- and, either way, the directory a module or unit
    /// compiled from it publishes its own .pmi/.tui interface file into
    /// (the same directory component InputFile's own path supplied to
    /// frontendPC1Main; "." when SourceName has no directory component of
    /// its own, matching writePMIFiles/writeTUIFile's shared fallback).
    std::string SourceName;

    /// Mirrors Scanner's own two constructors (see Scanner.h): std::nullopt
    /// (the default) reads SourceName from disk, exactly as frontendPC1Main
    /// always does; a set Buffer compiles its content instead, with
    /// SourceName then serving only as the display name a Diagnostic
    /// points into and the .pmi/.tui base directory described above --
    /// nothing on disk at that path is read.
    std::optional<std::string> Buffer;

    /// -dump-tokens: stop after lexing and make CompilationResult::Output
    /// the token stream's own text instead of compiled output.
    bool DumpTokens = false;
    /// -dump-parse-tree: stop after parsing, before Sema runs at all.
    bool DumpParseTree = false;
    /// -dump-ast: stop after Sema succeeds, before any codegen or interface
    /// file is published.
    bool DumpAst = false;
    /// -dump-vmt: same stopping point as DumpAst, printing virtual method
    /// tables instead of the AST.
    bool DumpVmt = false;
};

/// The result of one compile() call.
struct CompilationResult {
    /// Whether the compile succeeded outright. False for a lexical, syntax
    /// or semantic error; for a module/unit interface file that failed to
    /// publish; or for Codegen's own internal module-verification failure.
    /// Says nothing about what a caller that goes on to write Output
    /// somewhere else (a file, a socket, ...) finds when it does --  that
    /// write's own success is that caller's own concern, the same way
    /// frontendPC1Main checks its own -o/stdout write apart from this.
    bool Success = false;

    /// What this compile would have produced -- LLVM IR text, or (with one
    /// of the CompilationRequest::Dump* flags set) that dump mode's own
    /// text -- or std::nullopt if the pipeline never reached the point of
    /// producing anything at all (a lexical, syntax or semantic error, or a
    /// failure publishing a module's .pmi / a unit's .tui, all fail the
    /// compile before there is anything to emit). A *present* Output can
    /// still be empty, or still accompany Success == false: Codegen failing
    /// its own internal LLVM module verification writes nothing to its
    /// destination but still counts as "reached the emit stage", the same
    /// distinction frontendPC1Main itself preserves when deciding whether
    /// to even open its own -o file.
    std::optional<std::string> Output;

    /// Every diagnostic this compile reported, in report order. Diagnostic
    /// itself carries no source text to point at -- Loc is a bare offset
    /// into a SourceManager this call does not hand back -- so a caller
    /// that wants a formatted "file:line:col: severity: message" the way
    /// plang's own CLI prints (DiagnosticPrinter.h) has to keep its own
    /// SourceManager; Message alone, already fully formatted text with no
    /// further location context, is what every existing in-process caller
    /// of this shape already reads (test/unittests/Support/TestHelper.h's
    /// own check(), which this mirrors on purpose).
    std::vector<Diagnostic> Diagnostics;
};

/// Compiles \p Request's source through the same pipeline frontendPC1Main
/// drives from argv, entirely in-process: no subprocess, no implicit stdout/
/// stderr/exit-code protocol to parse back apart, and -- when
/// Request.Buffer is set -- no real source file on disk at all. See
/// CompilationRequest's own comment for the two constraints (process-global
/// locale selection, and interface-file publishing as a side effect) this
/// does not change.
[[nodiscard]] CompilationResult compile(const CompilationRequest& Request);

} // namespace plang
