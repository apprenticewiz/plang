//===- scanner-fuzzer.cpp - libFuzzer target for Scanner ----------------===//
//
// Feeds raw, arbitrary bytes straight to Scanner's in-memory-buffer
// constructor (no filesystem I/O -- see Scanner.h's second constructor)
// and drains every token it produces. Scanner's own contract is that it
// never throws and always terminates at Eof, no matter how malformed the
// input is; this target exists to find real-world violations of that
// contract (an infinite loop, an out-of-bounds read/write ASan catches, an
// unbounded recursion blowing the C++ stack) that ~2,900 well-formed
// test/**/*.pas files exercise only by accident, not by design.
//
// Part of issue #183 Phase 1 (cheap, additive fuzzing infrastructure).
// Build with -DPLANG_ENABLE_FUZZERS=ON; see fuzz/README.md for how to run
// this for longer than CI's short per-PR smoke budget.
//===----------------------------------------------------------------------===//

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Basic/SourceManager.h"
#include "plang/Lex/Scanner.h"

#include <cstdint>
#include <cstddef>
#include <string>

using namespace plang;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
    std::string Content(reinterpret_cast<const char*>(Data), Size);

    // -std=turbo is the widest dialect (superset of switch/include
    // directives, `//` comments, hex literals, ...), so scanning under it
    // exercises the most Scanner code paths per input byte.
    LangOptions Opts;
    Opts.Std = LangOptions::Standard::Turbo;

    DiagnosticsEngine Diags;
    SourceManager     SM;
    Scanner Sc(SM, "<fuzz-scanner>", Content, Diags, Opts);

    // next() itself guarantees repeated calls after Eof keep returning Eof,
    // so this loop is bounded by the token stream reaching Eof, not by any
    // artificial cap here -- the whole point is to let libFuzzer's own
    // per-run timeout (see fuzz/README.md) catch a real hang rather than
    // have this target quietly paper over one.
    for (;;) {
        Token T = Sc.next();
        if (T.Kind == TokenKind::Eof) break;
    }
    return 0;
}
