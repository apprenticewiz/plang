//===- pmi-fuzzer.cpp - libFuzzer target for .pmi interface loading -----===//
//
// Exercises the EP §6.11 separate-compilation path (Sema::loadPMI /
// Sema::loadPMIFromBuffer, lib/Sema/Sema.cpp) with raw, arbitrary bytes
// standing in for a .pmi module-interface file's content.
//
// Sema::loadPMI itself is private and reads a file *path*, not a buffer --
// fuzzing it directly would mean writing a fresh temp file per libFuzzer
// mutation, which is both slow (real filesystem I/O on every one of
// millions of iterations) and awkward (a corpus of on-disk paths instead of
// in-memory byte strings, unlike every other libFuzzer target in this
// directory). Two ways to fix that were considered:
//
//   1. A `friend` declaration in Sema.h naming a free function in this file,
//      reaching into loadPMI's existing private implementation as-is.
//   2. Split loadPMI into a thin file-reading wrapper (unchanged, still
//      private, still what every real caller uses) around a new
//      buffer-taking core, loadPMIFromBuffer, that does the actual
//      parsing/resolving work loadPMI used to do inline.
//
// (2) was chosen: it is less invasive (no `friend` edge from a stable header
// out to a single .cpp file under fuzz/, which every future reader of
// Sema.h would have to notice and understand), and it leaves a genuinely
// more testable seam behind even outside of fuzzing -- a buffer-taking core
// is also what a hypothetical future unit test for "does a malformed-but-
// not-crashing .pmi report the right diagnostic" would want, without that
// test needing a TempFile either. The buffer-taking core stays private
// itself; only a narrow, fuzz-only, pass/fail wrapper
// (Sema::fuzzLoadPMIFromBuffer, gated on the same PLANG_ENABLE_FUZZERS
// macro this whole target is gated on) is exposed -- see Sema.h's own
// comment on that method for why it returns bool rather than the private
// PMILoadResult type.
//
// Part of issue #183 Phase 1 (cheap, additive fuzzing infrastructure).
// Build with -DPLANG_ENABLE_FUZZERS=ON; see fuzz/README.md for how to run
// this for longer than CI's short per-PR smoke budget.
//===----------------------------------------------------------------------===//

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Sema/Sema.h"

#include <cstdint>
#include <cstddef>
#include <string>

using namespace plang;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    std::string Content(reinterpret_cast<const char*>(Data), Size);

    // loadPMIFromBuffer itself forces ISO10206 (EP) mode on the wrapped
    // buffer regardless of what Opts here says (see its own comment), so
    // the Opts passed to Sema only matters for whatever it consults outside
    // that one call -- left default (ISO 7185) the same way a real caller
    // compiling an ordinary program would have it before any 'uses' clause
    // is processed.
    DiagnosticsEngine Diags;
    Sema Sema(Diags);

    // The key a real caller would look for is whatever module name a
    // resolved search-path candidate was expected to hold; a fuzz target
    // has no such expectation, so a fixed placeholder is used throughout.
    // Both the matched ("this key's interface parses and resolves cleanly")
    // and unmatched ("wrong module name") paths inside loadPMIFromBuffer
    // are still reachable -- an input whose own module heading happens to
    // spell "fuzzpmimod" hits the former, everything else hits the latter,
    // and libFuzzer's coverage-guided mutation will find that spelling on
    // its own given enough runs, the same way it discovers any other
    // coverage-gated literal.
    (void)Sema.fuzzLoadPMIFromBuffer("fuzzpmimod", Content);
    return 0;
}
