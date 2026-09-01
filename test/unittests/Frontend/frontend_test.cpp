// Issue #179: plang::compile() -- a library-shaped entry point into the
// same pipeline frontendPC1Main drives from argv, for an embedder that
// wants structured CompilationResult data (Success, Output, Diagnostics)
// rather than a subprocess's exit code and scraped stdout/stderr.
//
// GoogleTest, not lit, for the same reason test/unittests/Sema's own
// permanent exceptions are: this is testing compile()'s own C++ API surface
// -- what it returns for buffer input, and the *structured* Diagnostic data
// (Severity/Message), not just an exit code -- which has no CLI-observable
// proxy at all. TestHelper.h's own check() is exactly this shape already,
// just Sema-only and private to the test tree; this is that same
// TestHelper.h, but exercising the public compile() API instead, one layer
// further down the pipeline (through Codegen, not just Sema).

#include "plang/Frontend/Compilation.h"

#include "plang/Basic/Diagnostic.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

using namespace plang;

namespace {

/// Whether any Diagnostic in \p Diags is an Error whose Message contains
/// \p Sub -- the same substring check test/unittests/Support/TestHelper.h's
/// own SemaResult::hasError gives Sema-only callers, reimplemented here
/// rather than shared, since it is three lines and pulling in TestHelper.h
/// would also pull in its file-backed check() this test does not want.
bool hasError(const std::vector<Diagnostic>& Diags, const std::string& Sub) {
    for (const auto& D : Diags)
        if (D.Severity == DiagSeverity::Error &&
            D.Message.find(Sub) != std::string::npos)
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// A successful compile, from an in-memory buffer
// ---------------------------------------------------------------------------

TEST(Compile, BufferInputProducesLlvmIrAndNoDiagnostics) {
    CompilationRequest Req;
    Req.SourceName = "<buffer>";
    Req.Buffer     = "program hello(output);\n"
                     "begin\n"
                     "  writeln('hi')\n"
                     "end.\n";

    const CompilationResult Res = compile(Req);

    EXPECT_TRUE(Res.Success);
    ASSERT_TRUE(Res.Output.has_value());
    // Codegen::emit writes a real LLVM module; "ModuleID" is the same marker
    // test/Driver/PC1/output-write-failure-is-a-nonzero-exit-not-silent-
    // success.pas's own IR check already pins down for the CLI path this
    // shares its pipeline with.
    EXPECT_NE(Res.Output->find("ModuleID"), std::string::npos);
    EXPECT_TRUE(Res.Diagnostics.empty());
}

// ---------------------------------------------------------------------------
// Failure mode 1: a syntax error (the Parser never returns a Program)
// ---------------------------------------------------------------------------

TEST(Compile, SyntaxErrorFailsWithNoOutputAndAStructuredDiagnostic) {
    CompilationRequest Req;
    Req.SourceName = "<buffer>";
    // Missing 'end.' -- Parser::parse() fails outright, so compileRequest
    // never reaches an output-producing action at all.
    Req.Buffer = "program broken(output);\n"
                "begin\n"
                "  writeln('hi')\n";

    const CompilationResult Res = compile(Req);

    EXPECT_FALSE(Res.Success);
    // std::nullopt, not just an empty string: the pipeline never reached
    // Codegen::emit (or any dump mode) at all for a Parser failure -- see
    // compileRequest's own comment on what Output distinguishes.
    EXPECT_FALSE(Res.Output.has_value());
    ASSERT_FALSE(Res.Diagnostics.empty());
    EXPECT_TRUE(std::any_of(Res.Diagnostics.begin(), Res.Diagnostics.end(),
                            [](const Diagnostic& D) {
                                return D.Severity == DiagSeverity::Error;
                            }));
}

// ---------------------------------------------------------------------------
// Failure mode 2: a semantic error (Parser succeeds, Sema::check fails)
// ---------------------------------------------------------------------------

TEST(Compile, SemanticErrorFailsWithNoOutputAndTheUndefinedIdentifierNamed) {
    CompilationRequest Req;
    Req.SourceName = "<buffer>";
    // Parses fine; Sema rejects the undefined identifier `undeclaredVar`.
    Req.Buffer = "program broken(output);\n"
                "begin\n"
                "  writeln(undeclaredVar)\n"
                "end.\n";

    const CompilationResult Res = compile(Req);

    EXPECT_FALSE(Res.Success);
    EXPECT_FALSE(Res.Output.has_value());
    EXPECT_TRUE(hasError(Res.Diagnostics, "undeclaredVar"));
}

// ---------------------------------------------------------------------------
// A -dump-tokens-equivalent request still produces Output on a scan-only
// failure path being avoided -- covers the "Output present, Success can
// still be true independent of dump flags" combination via DumpAst.
// ---------------------------------------------------------------------------

TEST(Compile, DumpAstStopsBeforeCodegenButStillProducesOutput) {
    CompilationRequest Req;
    Req.SourceName = "<buffer>";
    Req.Buffer     = "program hello(output);\n"
                     "begin\n"
                     "  writeln('hi')\n"
                     "end.\n";
    Req.DumpAst = true;

    const CompilationResult Res = compile(Req);

    EXPECT_TRUE(Res.Success);
    ASSERT_TRUE(Res.Output.has_value());
    // Not LLVM IR -- the AST dump's own text, so no "ModuleID" this time.
    EXPECT_EQ(Res.Output->find("ModuleID"), std::string::npos);
    EXPECT_FALSE(Res.Output->empty());
}

// ---------------------------------------------------------------------------
// The DiagnosticOptions -Werror knob still applies to compile(), the same
// as it does to frontendPC1Main's own -Werror.
// ---------------------------------------------------------------------------

TEST(Compile, WerrorTurnsAWarningIntoAFailure) {
    CompilationRequest Req;
    Req.SourceName = "<buffer>";
    // 'unused' is declared but never read or written -- warn_unused_variable,
    // not an error, under the default policy.
    Req.Buffer = "program p(output);\n"
                "var unused: integer;\n"
                "begin\n"
                "  writeln('hi')\n"
                "end.\n";

    const CompilationResult Warn = compile(Req);
    EXPECT_TRUE(Warn.Success);
    EXPECT_TRUE(std::any_of(Warn.Diagnostics.begin(), Warn.Diagnostics.end(),
                            [](const Diagnostic& D) {
                                return D.Severity == DiagSeverity::Warning;
                            }));

    Req.DiagOpts.WarningsAsErrors = true;
    const CompilationResult AsError = compile(Req);
    EXPECT_FALSE(AsError.Success);
}
