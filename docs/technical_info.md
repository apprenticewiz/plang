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
enumeration, the spellings, the scanner's keyword table and the set of words
only Extended Pascal reserves are all generated.  The command-line options are
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
`test/Sema/sema_test.cpp` is what covers that direction: one program per shared
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

1511 tests, in six binaries:

| Binary                              | Tests | What it covers                        |
|-------------------------------------|-------|---------------------------------------|
| `test/Lex/scanner_test`             | 139   | Tokens, literals, keywords, EP gating |
| `test/Parse/parser_test`            | 130   | Declarations, statements, expressions |
| `test/Sema/sema_test`               | 148   | Name resolution and type checking     |
| `test/Driver/driver_test`           | 716   | Compile, link, run, compare output    |
| `test/Conformance/conformance_test` | 377   | The Pascal-P5 ISO 7185 suite          |
| `test/Acceptance/acceptance_test`   | 1     | The Pascal Acceptance Test            |

The first three are unit tests over one phase each.  `driver_test` is the
end-to-end suite: it compiles a program, links it, runs it, and checks what it
printed and what it exited with, which is the only way to test code generation
and the runtime.

### The Pascal-P5 conformance suite

377 tests derived from the Pascal-P5 ISO 7185 conformance suite, now in the
public domain.  319 of them are programs that break a rule the standard states,
and pass when plang rejects them for the right reason; 58 are programs that
conform, and pass when plang accepts them.  They exercise the scanner, the
parser and semantic analysis — a rejection test is answered before any code is
generated — so a result here is a statement about conformance in that sense and
not about the correctness of the code produced.  That is what `driver_test` and
the acceptance test are for.

### The acceptance test

From the same suite, one standard program of three thousand lines that uses
nearly all of ISO 7185 at once.  It is compiled, executed, and compared line
for line against expected output audited twice: against the `s/b` ("should be")
annotation the program prints beside each of its 743 checks, and against the
output of the Pascal-P5 interpreter distributed with the suite.

Where the rejection tests ask whether a construct written wrongly is caught,
this asks whether the language works when it is all put together — and the two
questions do not have the same answer.

### Building and running it

The suite is off by default, since it needs GoogleTest and is the larger part
of the build; a release build takes about 9 seconds without it and about 59
with.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPLANG_ENABLE_TESTS=ON
cmake --build build -j$(getconf _NPROCESSORS_ONLN)
ctest --test-dir build -j$(getconf _NPROCESSORS_ONLN)
```

`ctest` works in a build configured without the tests as well, and reports that
it found none.

To run one binary directly, with the usual GoogleTest filters:

```bash
./build/test/Driver/driver_test --gtest_filter='SubrangeCompare.*'
```

### Sanitizers

`-DPLANG_SANITIZE=` takes the list through to `-fsanitize=`:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DPLANG_ENABLE_TESTS=ON -DPLANG_SANITIZE=address,undefined
```

UBSan's `vptr` check is switched off with it, because the libraries are built
`-fno-rtti` by LLVM convention and that check needs RTTI; left on, it reports
every `shared_ptr` control block as having an invalid vptr.
