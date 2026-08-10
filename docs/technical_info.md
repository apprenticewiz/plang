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

Diagnostics are catalogued in `.def` files split by the phase that raises them,
and reached by identifier — `diag::err_undefined_identifier` — with the message
text kept in a separate translation unit so that it can be translated.

Token kinds are likewise one list, `Basic/TokenKinds.def`, from which the
enumeration, the spellings, the scanner's keyword table and the set of words
only Extended Pascal reserves are all generated.  The command-line options are
one list in the same way, `Driver/Options.def`, read by both the driver and the
front end so that the two cannot drift apart in what they accept or in what
they say they accept.

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
`.def` catalogues are x-macros rather than TableGen, which does the same job at
this scale without a build-time generator.

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
rather than about a program.  They are catalogued in
`Basic/DiagnosticDriverKinds.def` alongside the rest and pass through the same
`DiagnosticsEngine`, which is the one place the policy below is applied,
whichever phase raised it.  A driver diagnostic went straight to stderr before,
with its own idea of when to use colour, which put it outside all of it.

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
| `unrecognised-argument` | An argument beginning with `-` that matches no option     |

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

### Colour

Colour is used when standard error is a terminal, and
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
cmake --build build -j$(nproc)
ctest --test-dir build -j$(nproc)
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
