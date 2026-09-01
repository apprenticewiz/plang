# `test/` — lit-based tests

This tree holds plang's `lit`+`FileCheck`-based tests: standalone `.pas`
files with `RUN:`/`CHECK:` directives embedded in comments, run by LLVM's
`lit` test runner. It mirrors how Clang splits `clang/test/` (lit,
black-box, drives the compiler as a subprocess) from `clang/unittests/`
(GoogleTest, tests internal C++ APIs directly, in-process) — plang's own
counterpart to `clang/unittests/` is [`test/unittests/`](unittests/), not
a separate top-level tree, the same relative nesting Clang itself uses.

**`test/unittests/` (GoogleTest) is not a leftover — it's a permanent,
deliberate tier**, not something migrating away. Tests that construct
`Scanner`/`Parser`/`Sema`/etc. objects directly and inspect them
in-process live there as GoogleTest, by design; nothing under
`test/unittests/` today is a candidate for further conversion. Tests that
spawn the real `plang` binary and assert on its exit code, stdout,
stderr, or emitted IR live here, under `test/`. See
[issue #34](https://github.com/apprenticewiz/plang/issues/34) and
[issue #43](https://github.com/apprenticewiz/plang/issues/43) for the
full rationale, phased rollout, and per-category conversion idioms behind
this split.

## A load-bearing syntax constraint

plang implements ISO 7185 §6.1.8's comment rule literally: `{ ... }` and
`(* ... *)` are each closed by **either** terminator — a bare `}` can
never appear inside any plang comment, regardless of which delimiter
opened it. Two consequences for every test file in this tree:

1. Every `RUN:`/`CHECK:` directive block opens and closes its comment
   delimiter **on its own line** — a directive never shares a line with
   the delimiter, since lit's own directive parser takes `KEYWORD:` to
   end-of-line, unanchored, and would otherwise swallow the closing `}`
   or `*)` into the command/pattern.
2. FileCheck's own `{{regex}}` curly-brace capture syntax can **never**
   be written literally in a `.pas` file. Use `[[NAME:regex]]`
   square-bracket capture/back-reference syntax instead, keeping the
   regex portion itself free of `{`/`}` (`![0-9]+`, `.*`, etc. — this has
   been sufficient for every case examined so far, including DWARF
   scope-graph assertions).

House style: use `(* *)` for directive blocks, not `{ }` — purely to keep
them visually distinct from ordinary Pascal commentary that might already
use `{ }` elsewhere in the same file; both delimiters are equally subject
to the same-terminator rule above.

## Running the tests

- Everything: `ninja check-lit` (or `cmake --build build --target check-lit`)
- One category: `ninja check-lit-<Category>`, e.g. `check-lit-Conformance`
- Via CTest: `ctest -R 'lit-<Category>'` — one CTest entry per top-level
  directory here, not per test file (LLVM-idiomatic granularity)
- A single test or pattern during development, the way LLVM contributors
  actually iterate day to day:
  `lit --filter='<regex>' build/test/<Category>`

## Substitutions available in `RUN:` lines

| Substitution | Expands to |
|---|---|
| `%plang` | the built `plang` binary, plus `PLANG_TEST_EXTRA_FLAGS`/`--param optlevel=` if set |
| `%plang_ep` | `%plang -std=iso10206` |
| `%plang_run` | `%plang %s -o %t && %t` (compile, link, run — no extra flags) |
| `%plang_ep_run` | `%plang_ep %s -o %t && %t` |
| `%run` | `PLANG_TEST_RUN_WRAPPER` if set (e.g. the guardheap allocator), empty otherwise — **always wrap a just-built program's execution as `%run %t`, never a bare `%t`**, or this hook silently stops applying to that one file with no failure signal |
| `%FileCheck`, `not`, `split-file`, `%s`, `%t`, `%T` | lit/LLVM defaults, via `lit.llvm` |

## Test timeouts

Every lit invocation in this project (`test/CMakeLists.txt`'s `LIT_ARGS`,
and the guardheap step in CI) passes `--timeout 300` (issue #189): any
single test that runs past 300s is killed and reported as a failure
rather than left to hang a CI job indefinitely.

This is a genuinely global, process-wide cap — **there is no working
per-test or per-directory override** in the lit version this project
targets. `lit`'s own `main.py` reads a single `lit_config.
maxIndividualTestTime` value at startup, and explicitly makes the
`--timeout` command-line flag win over anything a `lit.cfg.py`/
`lit.local.cfg` sets (confirmed by reading `lit/main.py`'s own "Command
line overrides configuration for maxIndividualTestTime" logic, and by
reproducing it directly). A `lit.local.cfg` setting `config.
maxIndividualTestTime` in some subdirectory looks plausible from lit's
docs but has no effect once a `--timeout` flag is present on the command
line, which every invocation in this project always has. Don't add one
expecting it to work.

300s was sized off the slowest test observed in this suite under the
*heaviest* real instrumentation: the deliberate O(n) layout stress test
(`test/CodeGen/CodegenRecord/laying-out-tens-of-thousands-of-fields-is-
not-quadratic.pas`) legitimately takes longer than the original 120s
budget under the `asan + ubsan` CI job's sanitizer overhead. If a future
test needs still more headroom, the only real lever is raising this one
global number (in both places it's set) — there is no cheaper, more
targeted mechanism available today.

`REQUIRES: fpc-binary` (not bare `fpc` — `include/plang/Basic/Dialects.def`
already reserves that name for a future, unrelated `-std=fpc` plang
dialect) gates a test on a real, working `fpc` install. The feature
itself is live — `test/lit.cfg.py` probes for a working `fpc -iV` and
adds `fpc-binary` to `config.available_features` when it succeeds — but
nothing under `test/` uses it yet: the small, deliberately-scoped FPC
differential-testing area sketched in issue #34 (`test/Compat/FPC/`,
gated on this feature, for spec-ambiguous/dialect-boundary constructs)
was never actually built. Until it is, `REQUIRES: fpc-binary` is
infrastructure with no consumer — don't go looking for `test/Compat/FPC/`,
it doesn't exist.
