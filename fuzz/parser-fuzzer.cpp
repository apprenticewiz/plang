//===- parser-fuzzer.cpp - libFuzzer target for Scanner+Parser ----------===//
//
// Feeds raw, arbitrary bytes through Scanner's in-memory-buffer constructor
// and then through Parser::parse(), the same Scanner->Parser pipeline every
// real compile drives, just fed adversarial mutated bytes instead of a real
// .pas file. Parser::parse() already bounds its own recursive-descent depth
// (see ExprDepthScope in Parser.h, added against exactly this kind of
// adversarial nesting), so this target's job is to catch anything that
// slipped past that bound, or any other memory-safety issue ASan can catch,
// rather than to prove the bound exists in the first place.
//
// Part of issue #183 Phase 1 (cheap, additive fuzzing infrastructure).
// Build with -DPLANG_ENABLE_FUZZERS=ON; see fuzz/README.md for how to run
// this for longer than CI's short per-PR smoke budget.
//===----------------------------------------------------------------------===//

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Lex/Scanner.h"
#include "plang/Parse/Parser.h"

#include <cstdint>
#include <cstddef>
#include <string>

using namespace plang;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    std::string Content(reinterpret_cast<const char*>(Data), Size);

    // -std=turbo again, for the same widest-dialect-coverage reason as the
    // Scanner target -- see its own comment.
    LangOptions Opts;
    Opts.Std = LangOptions::Standard::Turbo;

    DiagnosticsEngine Diags;
    SourceManager     SM;
    Scanner Sc(SM, "<fuzz-parser>", Content, Diags, Opts);
    Parser  P(std::move(Sc), Diags, Opts);

    // parse() returns nullptr on a syntax error, having already recorded
    // diagnostics -- an expected, common outcome for mutated input, not a
    // finding by itself.  What this target watches for is a crash, a
    // sanitizer report, or a hang, none of which show up in the return
    // value at all.
    (void)P.parse();
    return 0;
}
