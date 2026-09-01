# plang Technical Information

How plang is built and how it is checked.  The [README](../README.md) is the
overview; this is the detail behind it.

The other documents each answer a different question.
[`conformance.md`](conformance.md) is the ISO 7185 clause 5.1 documentation —
what the implementation defines, what it leaves dependent, and which of the
standard's *errors* it reports.  [`modules.md`](modules.md) covers Extended
Pascal modules and separate compilation.  `plang(1)` is the reference for the
command line, and describes every option and every warning in full.

## Architecture

### The driver and the front end

plang follows the clang driver model, and recognises many of the same options.
The front end is factored out into a shared library that the driver links
against, so that it can be reused by other things that need to read Pascal — a
language server, for one.

The phases are the conventional ones, each in its own directory under `lib/`:

| Directory   | Phase                                                       |
|-------------|-------------------------------------------------------------|
| `Basic/`    | Source locations, diagnostics, token kinds, language options |
| `Lex/`      | Scanner                                                     |
| `Parse/`    | Parser, producing the AST                                   |
| `AST/`      | The node types, the type context, and the AST printer       |
| `Sema/`     | Name resolution, type checking, control-flow warnings       |
| `CodeGen/`  | LLVM IR generation                                          |
| `Driver/`   | Command line, job scheduling, the assembler and linker      |
| `Frontend/` | The `-pc1` entry point the driver invokes                   |

### Following clang

Several of clang's design choices are followed where Pascal gives the same
reason for them.

Diagnostics are cataloged in `.def` files split by the phase that raises them,
and reached by identifier — `diag::err_undefined_identifier`.  The English text
is written in those same files and compiled in; a translation of it is read at
run time from a `.po` file.  See "Translations" below.

Token kinds are likewise one list, `Basic/TokenKinds.def`, from which the
enumeration, the spellings, the scanner's keyword table and which dialects
reserve which words are all generated.  The command-line options are
one list in the same way, `Driver/Options.def`, read by both the driver and the
front end so that the two cannot drift apart in what they accept or in what
they say they accept.

The required procedures and functions are one list too, `Basic/Builtins.def`:
each name once, with the dialects that require it, its arity and its result
type.  Sema declares them from it and checks their arity from it, and a
resolved call carries the `BuiltinID` rather than a flag, so code generation
acts on what Sema decided instead of matching the spelling against a list of
its own.  This was three lists that could not see each other, and the drift it
invites had already happened: nineteen of the Extended Pascal names were
declared only under `-std=iso10206` and so came back as *undefined* under
`-std=iso7185`, while ten others correctly said what they were.

The AST uses LLVM's RTTI, with `classof` and `isa`/`dyn_cast` rather than a
visitor.

A `SourceManager` owns the text of every buffer and lays them out in one
coordinate space, so a `SourceLocation` is a four-byte offset into it rather
than a filename and a line and a column.  That is what lets a diagnostic quote
the line it is about, and it keeps an AST node to sixteen bytes with no
allocation.

`Parse`, `Sema` and `CodeGen` are each split by grammatical category —
`ParseDecl`, `ParseExpr`, `ParseStmt` and so on.

### Departing from clang

Where clang's reason does not apply, plang does not follow it.  Most of clang's
`lib/Lex` is the C preprocessor, and Pascal has no macros, no `#include` and no
pragmas, so plang's scanner is one file.  There is no tentative parsing,
because Pascal has no declaration-versus-expression ambiguity to resolve.  The
`.def` catalogs are x-macros rather than TableGen, which does the same job at
this scale without a build-time generator.

`tools/plang-po` is a build-time generator, but not of that kind: nothing the
compiler is built from comes out of it.  It reads the same `.def` files the
compiler does and writes the `.po` a *translator* works from, which has to be a
file on disk because the person editing it is not building plang.

### Language and style

The source is aggressive in its use of modern C++; a compiler supporting C++23
is required.  It follows the
[LLVM coding standards](https://llvm.org/docs/CodingStandards.html) as far as
is practical, though this is not an official LLVM project.

## Diagnostics

A diagnostic names the file, the line and the column, then quotes the line and
puts a caret under the column:

```
prog.pas:5:14: warning: 42 is outside the range 1..10; this is an error whenever it is reached
    count := 42;
             ^
```

The line is reproduced as it was written, tabs included, so the caret lands
under the right character whatever the indentation.

A diagnostic with no place in the source is printed under the program name:

```
plang: error: no input files
```

These are the driver's, and they are about the command line or the toolchain
rather than about a program.  They are cataloged in
`Basic/DiagnosticDriverKinds.def` alongside the rest and pass through the same
`DiagnosticsEngine`, which is the one place the policy below is applied,
whichever phase raised it.  A driver diagnostic went straight to stderr before,
with its own idea of when to use color, which put it outside all of it.

### Dialects

plang answers two different questions about the language it is compiling, and
keeping them apart is what lets a second dialect exist at all.

`LangOptions::extendedPascal()` asks *which standard this is*. It is the right
question for the thirty-odd extensions that belong to Extended Pascal alone —
schema types, `type of`, `bindable`, `restricted`, `**`, `><`, `value`,
`for ... in`, modules — and twenty-five sites ask it. Turbo Pascal must never
inherit any of them: `Vector(10)` is a call there, not a type.

`LangOptions::has(Feature::X)` asks whether the dialect has a *capability*, and
is the right question for the handful more than one dialect has — declaration
order, constant expressions in `const`, `case` ranges and default arms,
subrange bound expressions, empty string literals, underscores in identifiers,
char concatenation. Those are listed in `Basic/LangFeatures.def` with the
dialects that have them, and eight sites ask it.

Only shared capabilities are listed. Writing all thirty out as a dialect matrix
would mean transcribing an Extended Pascal column by hand, and a mistranscribed
cell changes Extended Pascal silently: both conformance corpora are ISO 7185
programs, so an ISO 7185 mode that grew an extension — or an Extended Pascal
mode that lost one — passes all 377 of them. The `DialectGating` suite in
`test/unittests/Sema/sema_test.cpp` is what covers that direction: one program per shared
capability, accepted under a dialect that has it and refused with a *named*
diagnostic under one that does not. The named diagnostic matters — written as a
bare "was rejected", the `case ... else` pair passed with its gate deliberately
disabled, because ISO 7185 hands `otherwise` back as an identifier and the
program failed a token later for an unrelated reason.

A feature's dialects are derived from `Std` rather than stored, so there is no
seeded copy to fall out of step. Per-feature overrides, if FPC's
`{$MODESWITCH}` ever wants them, go behind `has()` without any call site
changing.

The dialect names themselves are in `Basic/Dialects.def`, which generates the
`Standard` enumeration, the `D_*` bits, and the validation both the driver and
the front end do — two processes that must agree, and which previously held
four copies of the list between them.

### The storage model

How wide a type is travels with the type. `Type::Width` and `Type::IsSigned`
are meaningful for Integer, Subrange, Enum and Boolean, and for Real the width
is the float width. ISO 7185 and Extended Pascal have one integer type and
stamp 64 on all of it, so `getIntNTy(ctx, Width)` is the i64 they have always
emitted; Turbo has Byte, ShortInt, Word, Integer, LongInt and Comp at four
widths, and the width is what `SizeOf` answers, what a variable typecast's
legality rule compares, and what a `file of T` image is made of.
`TypeContext::getInt(bits, signed)` interns them, so two `Word`s are one type
and `identical` — a pointer comparison — says so.

A subrange takes its host's width where the host is an integer. Over a char it
is stored as a full ordinal, which is what plang has always done; narrowing
that moves ISO 7185 and Extended Pascal layouts and waits for the dialect that
needs it.

**One size, checked against the other.** `Sema::byteSizeOf` answers without a
DataLayout, because a Turbo `const BufSize = 4 * SizeOf(Integer)` has to fold
before there is one. That is a second opinion about storage, and a second
opinion is what goes wrong quietly — a `SizeOf` that disagrees with the layout
sizes a `GetMem` or a `BlockRead` buffer wrong, and nothing says so until the
memory past the end of it is read back. So codegen asserts it against
`DataLayout::getTypeAllocSize` for every type it lowers, in every program it
compiles, and a disagreement is an ICE rather than corruption. Writing that
check is what found three things: that Sema's field list is flat and summing it
counts storage a variant's alternatives share, that a set aligns to sixteen
because it lowers to an i256, and that a schema instance has no size to give
because its denoters carry whichever instantiation was resolved last.

**`packed` packs.** ISO §6.4.3.1 leaves what it does to the implementation, and
plang used to do nothing with it. A `packed record` is an LLVM packed struct
now, in every dialect: Turbo needs it for `{$PACKRECORDS 1}` and for a record
image a real Turbo program can read, and a `packed` that packs nothing is a
word the language has that means nothing. Packedness is part of the
struct-type cache key — without it two records differing only in packing share
one struct, and the packed one gets the padded one's offsets.

### Positional compiler switches

Turbo Pascal's `{$R+}` is textual: it applies from where it is written to the
end of the file, so one compilation can check one loop and not the next.  There
is nowhere on `LangOptions` to say that — a flag there is a statement about the
whole compilation — so the answer lives in a table of the places the state
changed, `Basic/SwitchTable.h`, binary-searched by source location.  The
switches themselves are one list, `Basic/CompilerSwitches.def`, which carries
each one's letter, its long name, what Turbo starts it at, and whether plang
acts on it or merely records it so `{$IFOPT}` can answer truthfully.

The alternative designs were a field snapshotted onto every AST node, which
costs bytes on every node and makes the AST printer and the interface writer
learn about switches to stay correct, and a coarser map, which cannot answer
`{$IFOPT}` at all.

**A null table means the flag.**  ISO 7185 and Extended Pascal have no
directives, so they build no table, so every query returns the command-line
default and nothing is searched.  That is what keeps their generated code
exactly what it was: the 181 modules the conformance corpus and the acceptance
test produce, across `-std=iso7185`, `-std=iso10206` and `-fno-range-checks`,
are byte-identical either side of the change that introduced this.

The consumer today is range checking.  There are no directives yet to fill a
table in — that is the Turbo Pascal front end — so the positional behavior is
tested by building a table by hand and compiling through it in process, in
`test/unittests/CodeGen/switches_test.cpp`.  Doing it by hand is also the only way to
reach the case that matters most: a location *before* a change still gets the
old state, which a scanner reading in source order cannot produce.

The same `D_*` bits gate the builtins. A required procedure or function is
declared *whatever the dialect*, and `Builtins.def` says which ones may use it;
where the active dialect is not among them, the name resolves and is refused
for being another dialect's. That is deliberate, and it is why `cmplx` under
`-std=iso7185` names the dialect boundary rather than reporting an undefined
identifier. It does not reserve the name: a program remains free to declare its
own `date` or `cmplx`, which shadows the builtin as ISO §6.2.2.10 requires.

### Translations

Diagnostic messages can be translated; nothing else about plang can, and
nothing else about it is locale-sensitive.

The English lives in the four `Basic/Diagnostic*Kinds.def` catalogs and is
compiled in, so the compiler can always say what is wrong with a program
whatever else is missing.  A translation is a GNU gettext `.po` file read when
plang starts, keyed by the diagnostic's identifier rather than by its English —
`msgctxt "diag/err_undefined_identifier"` — so that rewording a message in
English does not silently untranslate it everywhere.

`tools/plang-po` writes `en_US.po` from the `.def` files at build time; it is
the base a translator copies, and generating it is what keeps it from drifting
away from what the compiler actually says.  It is a C++ tool rather than a
script because 53 of the 193 messages are written as several adjacent string
literals, and only the preprocessor concatenates those correctly.

The format is gettext's, but libintl is not linked: the format is what buys a
translator Poedit, Weblate and `msgmerge`, and macOS does not ship libintl in
`libSystem`.  `Basic/MessageCatalog.cpp` reads the subset plang writes, and is
deliberately strict about the rest — four escapes only, no plurals, no
non-UTF-8 — because it is the one part of the compiler that parses bytes
somebody else wrote.  A message's `%0..%9` may be reordered by a translation
but not dropped or invented; one that does not use the same set is refused and
the English is used for that message alone.

Everything that can go wrong ends in English rather than in an error: a missing
catalog, an unreadable one, one from a newer plang, one with a bad entry, one
still being written, and an entry the translator has marked `#, fuzzy`.  That
makes a catalog installed where plang cannot find it invisible, so `--version`
reports which one it resolved, and both CI install checks assert it — using
`qps_ploc`, a generated pseudo-locale in which every message is the English
wrapped in `[!` and `!]`.

Translations live in `po/`, one file per language, and are copied into the
build tree beside the generated ones.  A regional catalog is a delta over the
language below it — `es_MX.po` names only what Mexican usage spells differently
and `es.po` supplies the rest — so the loader reads the whole chain, least
specific first.  `fr` and `es` ship marked `#, fuzzy` and are therefore inert
until reviewed; `po/README.md` is the translator's instructions.

### Warnings

Twelve, all enabled by default, each with a name it can be turned off by.
`--help-warnings` lists them; `-Wno-<name>` disables one, `-w` disables all of
them, and `-Werror` turns those that remain into errors.

Most report an error ISO 7185 §5.1 f) 1) permits a processor to leave
unreported, and which plang does leave unreported in general.  The rest report
a construct that is well formed and cannot have been meant.  None of them
rejects a program unless `-Werror` is given.

| Name                    | Reports                                                   |
|-------------------------|-----------------------------------------------------------|
| `var-uninitialized`     | A variable read on a path that has not given it a value   |
| `for-var-after-loop`    | A for-statement's control variable read after the loop    |
| `result-not-always-set` | A path through a function that does not assign its result |
| `const-div-zero`        | A constant zero divisor                                   |
| `const-out-of-range`    | A constant assigned outside the subrange it goes into     |
| `case-not-exhaustive`   | A `case` that cannot cover its selector                   |
| `unreachable-code`      | A statement no path reaches                               |
| `compare-always`        | A comparison one operand's range has already settled      |
| `unused-variable`       | A variable declared and never mentioned again             |
| `unused-parameter`      | A parameter never named in the body                       |
| `label-unreachable`     | A label no `goto` names                                   |
| `unrecognized-argument` | An argument beginning with `-` that matches no option     |

The last is the only one about the command line rather than about the program,
and the only one that can be reported before a source file has been opened.

The first three follow the flow through a block, and report only where *no*
path reaching the statement gives the variable a value.  They say nothing at
all about a block that declares a label, since a `goto` may land anywhere the
label is in scope, nor about a variable a nested procedure can reach, nor about
a program that has already failed to compile.  `plang(1)` describes each
warning in full, and `conformance.md` says which standard errors go unreported.

### The error limit

`-ferror-limit=<n>` reports at most *n* errors and passes over the rest, which
spares the cascade that a single mistake early in a file can set off.  The
compilation still fails.  Zero, the default, means no limit, and warnings are
not counted against it.

### Color

Color is used when standard error is a terminal, and
`-f{,no-}color-diagnostics` says otherwise.  The driver and the front end are
separate processes and both print diagnostics, so both have to answer this;
the option is `Both` in the table, so the driver acts on it and hands it on,
and the two cannot disagree.

## The test suite

2282 tests, split across two harnesses (issue #34, issue #43) — GoogleTest
for in-process, C++-API-level tests, and LLVM's `lit`+`FileCheck` for
CLI-driven, black-box tests that spawn the real `plang` binary — mirroring
how Clang splits `clang/unittests/` from `clang/test/`. `test/README.md`
covers the lit side's own conventions in full; this section covers both.
This project adds tests continually, so treat the counts below as a
snapshot rather than a promise: `ctest --test-dir build -N` lists every
GoogleTest case and every `lit-<Category>` suite entry by name, and
`lit -q build/test` reports the live lit total — the figure `ctest -N`
alone cannot give, since it counts each lit suite as one entry regardless
of how many `.pas` files are inside it.

### GoogleTest (`test/unittests/`) — 61 tests, in six binaries

| Binary                                         | Tests | What it covers                                          |
|--------------------------------------------------|-------|-----------------------------------------------------------|
| `test/unittests/Basic/catalog_test`               | 16    | `.po`-reader/locale-selection internals the CLI can't observe |
| `test/unittests/Basic/source_manager_test`        | 5     | Source-buffer coordinate overflow                        |
| `test/unittests/Sema/sema_test`                   | 2     | The `Builtins` X-macro loop, driven over every entry in one in-process pass |
| `test/unittests/CodeGen/codegen_storage_test`     | 15    | `byteSizeOf`/`byteAlignOf` unit cases with no CLI-observable proxy |
| `test/unittests/CodeGen/codegen_switches_test`    | 15    | Positional `LangOptions` state no command-line flag re-derives |
| `test/unittests/Driver/driver_test`               | 8     | `versionDirLess`, the toolchain-directory version comparator (issue #250) |

Each is a permanent exception, not a migration backlog: every case
constructs a `Scanner`/`Parser`/`Sema`/`Codegen`/`Driver`/`MessageCatalog`
object directly and asserts on internal state the CLI has no way to
observe, or drives a loop over compile-time data too fine-grained to spawn
a process per entry. Everything else that once lived under GoogleTest —
`scanner_test`, `parser_test`, the bulk of the six binaries above, plus the
pre-issue-#34 `driver_test.cpp`, `codegen_test`, `ep_test`, `module_test`,
and the 377-file `Conformance/cases/` suite — has migrated to `test/`,
below. `test/unittests/Driver/driver_test` in the table above is unrelated
to that migrated file despite the shared name: it is a new, narrowly-scoped
exception added afterward for `versionDirLess` (issue #250), on the same
no-CLI-observable-proxy grounds as the rest of this table, not a returning
remnant of the binary issue #34 moved out of this directory.

### lit + FileCheck (`test/`) — 2221 tests, in eleven suites

| Suite              | Tests | What it covers                                        |
|---------------------|-------|----------------------------------------------------------|
| `test/Smoke`        | 1     | The toolchain itself is wired up correctly              |
| `test/Lex`          | 153   | Tokens, literals, keywords, EP gating                    |
| `test/Parse`        | 146   | Declarations, statements, expressions                    |
| `test/Sema`         | 218   | Name resolution and type checking                        |
| `test/Basic`        | 32    | Message-catalog selection and locale-chain fallback, via the CLI paths that surface them |
| `test/Conformance`  | 377   | The Pascal-P5 ISO 7185 suite                             |
| `test/Acceptance`   | 1     | The Pascal Acceptance Test                               |
| `test/CodeGen`      | 440   | What the generated code does when run                    |
| `test/EP`           | 394   | Extended Pascal, end to end                              |
| `test/Module`       | 258   | Modules and separate compilation                          |
| `test/Driver`       | 201   | The driver and the command line, including DWARF debug-info scope-graph assertions |

#### The Pascal-P5 conformance suite

377 tests derived from the Pascal-P5 ISO 7185 conformance suite, now in the
public domain.  319 of them are programs that break a rule the standard states,
and pass when plang rejects them for the right reason; 58 are programs that
conform, and pass when plang accepts them.  They exercise the scanner, the
parser and semantic analysis via `plang -dump-ast` — a rejection test is
answered before any code is generated — so a result here is a statement about
conformance in that sense and not about the correctness of the code produced.
That is what the CodeGen suite and the acceptance test are for.

#### The acceptance test

From the same suite, one standard program of three thousand lines that uses
nearly all of ISO 7185 at once.  It is compiled, executed, and compared line
for line against expected output audited twice: against the `s/b` ("should be")
annotation the program prints beside each of its 743 checks, and against the
output of the Pascal-P5 interpreter distributed with the suite.

Where the rejection tests ask whether a construct written wrongly is caught,
this asks whether the language works when it is all put together — and the two
questions do not have the same answer.

### Building and running it

The suite is off by default, since it needs GoogleTest and `lit`/`FileCheck`
and is the larger part of the build; a release build takes about 9 seconds
without it and about 59 with.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPLANG_ENABLE_TESTS=ON
cmake --build build -j$(getconf _NPROCESSORS_ONLN)
ctest --test-dir build -j$(getconf _NPROCESSORS_ONLN)
```

`ctest` works in a build configured without the tests as well, and reports that
it found none. By default it also works without `lit`/`FileCheck` on `PATH` —
the `check-lit-*` targets and their `lit-*` CTest entries are simply
unavailable, with a CMake-configure-time warning saying so (`pip install lit`
and reconfigure to pick them up). `-DPLANG_TESTS_REQUIRE_LIT=ON` turns that
warning into a configure-time error instead, for anyone who needs "lit/
FileCheck missing" to fail loudly rather than silently shrink the suite down
to the GoogleTest tier alone — CI builds this way (issue #184), since there a
missing tool is a packaging regression, not a matter of developer taste.
CI also runs `test/tools/assert-test-discovery.sh` right after Configure,
which independently re-derives the `lit-*` suite list from `ctest -N` and the
individual test count each suite discovers from `lit --show-tests`, and fails
if either looks too small — catching, for example, a single suite's
`add_plang_lit_suite()` call being deleted, which touches neither
`LIT_EXECUTABLE` nor `FILECHECK_EXECUTABLE` and so is invisible to
`PLANG_TESTS_REQUIRE_LIT` above.

To run one GoogleTest binary directly, with the usual GoogleTest filters:

```bash
./build/test/unittests/CodeGen/codegen_storage_test --gtest_filter='*'
```

To run one lit suite, or filter within it, the way LLVM contributors iterate
day to day:

```bash
lit build/test/CodeGen
lit --filter='SemaTypeIdentity' build/test/CodeGen
```

### Sanitizers

`-DPLANG_SANITIZE=` takes the list through to `-fsanitize=`:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DPLANG_ENABLE_TESTS=ON -DPLANG_SANITIZE=address,undefined
```

It is a `CACHE STRING`, not a `CACHE BOOL` -- `cmake-gui`/`ccmake` show it as
a free-form field (with `address`, `undefined`, `thread`, `leak`, `memory`,
and the combination above offered for discovery), not a checkbox. Configure
fails immediately with `message(FATAL_ERROR ...)` on any name outside that
list, so a typo (`-DPLANG_SANITIZE=adress`) is caught before the build
starts rather than producing an unsanitized binary or a late, easy-to-miss
compiler error.

UBSan's `vptr` check is switched off with it, because the libraries are built
`-fno-rtti` by LLVM convention and that check needs RTTI; left on, it reports
every `shared_ptr` control block as having an invalid vptr.

### Fuzzing (issue #183 Phase 1)

`-DPLANG_ENABLE_FUZZERS=ON` builds three libFuzzer targets under `fuzz/`:
`scanner-fuzzer` and `parser-fuzzer` feed raw, arbitrary bytes straight
through `Scanner`'s and `Parser`'s existing in-memory-buffer constructors (no
filesystem I/O involved); `pmi-fuzzer` exercises EP separate-compilation
`.pmi` interface loading through `Sema::loadPMIFromBuffer`, the buffer-taking
core `Sema::loadPMI` (private, file-path-only, used by every real caller) was
split into for exactly this reason — see `fuzz/pmi-fuzzer.cpp`'s own header
comment for the two designs considered and why the split was chosen over a
`friend` declaration.

Requires Clang (`-fsanitize=fuzzer` is a Clang/compiler-rt feature with no
GCC equivalent); configure fails immediately with a clear error under GCC
rather than failing later at the first fuzz-target compile.

```bash
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPLANG_ENABLE_FUZZERS=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz -j$(getconf _NPROCESSORS_ONLN)
```

Turning the option on applies `-fsanitize=fuzzer-no-link,address` to the
*whole* build, not just the three `fuzz/*.cpp` files — ASan and
SanitizerCoverage have to instrument `Scanner`/`Parser`/`Sema` themselves
(built into `plang_frontend`, which the fuzz targets link against) for a
fuzz run to catch a memory bug in the code actually being fuzzed. Each fuzz
target then adds plain `-fsanitize=fuzzer` (link-only) on top, which is what
pulls in libFuzzer's own `main()`/driver — scoped to just those three
targets so the `plang` driver and every test binary a `PLANG_ENABLE_TESTS=ON`
build alongside this still link.

Each target ships a seed corpus under `fuzz/corpus/<target>/`: a random
sample of real `.pas` files already in `test/` (not the full ~2,900-file
suite — a few hundred spanning every `test/` category is enough to seed
useful coverage without a disproportionate repo diff) for `scanner`/`parser`,
and one hand-written `.pmi`-shaped module interface for `pmi`. Run a target
against its own corpus for longer than CI's short per-PR smoke budget (see
below) by pointing it at a **copy** of that directory, not the tracked one
directly — libFuzzer writes newly-interesting inputs back into whatever
corpus directory it's given, which is exactly what you want for a real,
ongoing fuzzing session but not for `fuzz/corpus/` as checked into git:

```bash
cp -r fuzz/corpus/scanner /tmp/scanner-corpus
ASAN_OPTIONS=alloc_dealloc_mismatch=0 \
  ./build-fuzz/bin/scanner-fuzzer -max_total_time=600 /tmp/scanner-corpus
```

`ASAN_OPTIONS=alloc_dealloc_mismatch=0` works around a known ASan false
positive in LLVM's own `DenseMap`/`MemAlloc.cpp` bucket allocator (pairs
`operator new(nothrow)` with a plain `free()`, deliberately, in a way ASan's
stricter check flags even though it is safe on this platform) — not a plang
bug, and not specific to any one of the three targets.

CI (`fuzz-smoke` in `.github/workflows/ci.yml`) runs each target for a short,
fixed 60-second wall-clock budget on every PR against its checked-in seed
corpus, as a crash-regression smoke check — not deep fuzzing, and
deliberately not scheduled/long-running fuzzing infrastructure with its own
corpus-persistence and crash-triage story (that is Phase 2 of issue #183,
explicitly deferred).
