# Turbo Pascal in plang

`-std=turbo` selects Turbo Pascal 7 as plang reads it: the dialect's own
16-bit signed `Integer`, its keywords and operators (`@`, `shl`/`shr`, bitwise
`and`/`or`/`not`/`xor`, `Exit`/`Break`/`Continue`), its literal syntax
(`$hex`, `#code`, `^ctrl`, adjacent string/char gluing), its `case`-statement
and text-formatting rules, and — the subject of this document — a `{$...}`
compiler-directive system with no equivalent in ISO 7185 or Extended Pascal.
This is Tier 1 of the Turbo milestone: enough of the dialect to compile and
run real Turbo/FPC-`-Mtp` source, not (yet) the real-mode DOS surface
(`Seg`/`Ofs`/`Mem`/`Intr`/...), which plang rejects by name as targeting a
machine this compiler does not build for, or object types.

This document covers the compiler-directive system in the depth `plang(1)`
doesn't: every accepted `{$...}` directive, the predefined `{$IFDEF}` symbols,
how `{$I file}` resolves a path, and — because a compatibility dialect is
only honest if it says where it stops being one — every point where plang's
Turbo mode is known to diverge from what a real `fpc -Mtp` (this project's
stand-in for Turbo Pascal 7 itself, and empirically checked against
throughout) actually does.

---

## Directive syntax

A directive is a comment whose first character is `$`: `{$R+}` or the
alternate delimiter form `(*$R+*)`. Outside `-std=turbo` this is not special —
`{$anything}` is an ordinary brace comment, its contents never inspected,
under every other dialect. Inside `-std=turbo`, the text between the `$` and
the closing delimiter splits into a **Name** (the leading run of letters) and
an **Argument** (everything after, trimmed of surrounding whitespace), and
the name is matched against five categories in order: message directives,
conditional compilation, `{$I file}`/`{$INCLUDE file}`, `{$R+}`-style
switches, and the accept-and-ignore table. A name that matches none of them
is reported — `warning: unknown compiler directive 'NAME'` (`-Wno-directive-unknown`
silences it) — rather than silently passed over, on the view that a `{$R+}`
that does nothing and says nothing is worse than one that says so.

## Message directives

Borland Pascal 7's own seven, each `{$NAME text}` (the plain BP7 argument
form — *not* FPC's compound `{$MESSAGE <TYPE> text}`, which requires a
leading TYPE keyword `fpc -Mtp` itself insists on and BP7 never had):

| Directive | Severity | `-Wno-<name>` |
|---|---|---|
| `{$MESSAGE text}` | Info (always printed) | — |
| `{$INFO text}` | Info | — |
| `{$NOTE text}` | Info | — |
| `{$HINT text}` | Warning | `-Wno-directive-hint` |
| `{$WARNING text}` | Warning | `-Wno-directive-warning` |
| `{$ERROR text}` | Error (fails the compile) | — |
| `{$FATAL text}` | Error (fails the compile) | — |

`{$MESSAGE}`/`{$INFO}`/`{$NOTE}` are Info-severity, not warnings, so `-w` and
`-Wno-*` never suppress them — the same as any other `note:`. `{$ERROR}` and
`{$FATAL}` both fail compilation the way any other error does, so neither has
a `-Wno-` spelling either. Real `fpc -Mtp` distinguishes the two by *when*
they take effect: `{$ERROR}` reports and keeps compiling to the end of the
file, `{$FATAL}` reports and aborts the scan right there. plang's
diagnostics are collected and only inspected at a handful of fixed points
(after parsing, after Sema, ...), with no mid-scan unwind to hook an
immediate abort into (truncating the token buffer at that point was tried;
it just leaves the parser mid-construct, reporting a cascade of "expected X,
got end of file" noise once the compile has already failed either way). So
`{$FATAL}` gets its own diagnostic — text distinguishable from `{$ERROR}`'s —
but the same report-and-continue handling.

## Conditional compilation

`{$DEFINE symbol}` / `{$UNDEF symbol}` / `{$IFDEF symbol}` / `{$IFNDEF symbol}`
/ `{$ELSE}` / `{$ELSEIF symbol}` / `{$ENDIF}`, matching real Turbo/FPC syntax
and nesting rules. `{$DEFINE}`/`{$UNDEF}` are positional, like a switch: a
symbol becomes defined (or stops being) from that point in the source
forward, so the same symbol can be defined in one part of a file and
undefined in another. A dead `{$IFDEF}`/`{$IFNDEF}` branch is skipped as raw
text — nothing inside it is tokenized, diagnosed, or even looked at past a
nested directive's own name, exactly as real Turbo/FPC never evaluates a
branch that was never taken. This is why `{$IFDEF}`-guarded scaffolding can
contain code plang would otherwise reject; see
`test/Lex/ScannerTurbo/dead-conditional-branch-malformed-content-produces-no-diagnostic.pas`.

Two command-line flags seed the starting set before the file's own
directives run: `-d<symbol>` (equivalent to a `{$DEFINE symbol}` at the top
of the file) and `-u<symbol>` (equivalent to `{$UNDEF symbol}`). Multiple
`-d`/`-u` flags apply left to right, so a later `-u<symbol>` undoes an
earlier `-d<symbol>` for the same name and vice versa — the same rule two
in-source directives in that order would follow.

### Predefined symbols

Before any `-d`/`-u` or in-source directive runs, the front end seeds
`{$IFDEF}`'s symbol set with facts about the compilation's target
(`addPredefinedConditionalSymbols`, `lib/Frontend/Frontend.cpp`) — deliberately
minimal, just enough for the `{$IFDEF UNIX}`/`{$IFDEF LINUX}` idioms real
source already uses to work on the platforms plang actually targets (Linux
and macOS, x86_64 or aarch64; see the top-level README):

| Symbol | Set when |
|---|---|
| `UNIX` | The target OS is Linux or macOS |
| `LINUX` | The target OS is Linux |
| `DARWIN` | The target OS is macOS |
| `CPU32` | The target's pointer width is 32 bits |
| `CPU64` | The target's pointer width is 64 bits |
| `CPUX86_64` | The target architecture is x86_64 |
| `CPUAARCH64` | The target architecture is aarch64 |

The target is `--target=<triple>` if given, otherwise the host triple. Names
match FPC's own spelling of these facts (they describe the machine, not
which compiler is running), so source that already tests them ports
unmodified.

**`FPC` is deliberately never predefined**, permanently, not as a gap to
close later. Source guarded by `{$IFDEF FPC}` takes a branch written against
an FPC-only language feature this milestone does not implement (and may
never); predefining `FPC` would let such a branch compile as though it did,
which is worse than the branch simply not being taken. A program that wants
to detect "am I being compiled by plang" has nothing to test for that today —
there is no `{$IFDEF PLANG}` — since no real-world source depends on one yet.

Every predefined symbol is an ordinary member of the same set a program's own
`-u<symbol>` or `{$UNDEF}` can override; `-uUNIX` really does turn `{$IFDEF UNIX}`
false on a Linux build, the same as undefining any other symbol would.

### `{$IFOPT}` is not implemented

Real Turbo/FPC also has `{$IFOPT R+}`/`{$IFOPT R-}`, a conditional that
branches on a *switch's* current state rather than a `{$DEFINE}`d symbol's.
plang's switches are recorded into a position-keyed table
(`SwitchTable`, below) specifically so that a future `{$IFOPT}` can answer
correctly wherever it is written in a file — but `{$IFOPT}` itself is not
one of the five directive categories `dispatchDirective` recognizes today.
Writing it compiles as an unrecognized directive
(`warning: unknown compiler directive 'IFOPT'`), and the `{$ELSE}`/`{$ENDIF}`
that would normally close it fail with "no matching `{$IFDEF}`/`{$IFNDEF}`",
since nothing pushed a conditional frame for it to close. This is a real,
currently-open gap, not a documentation slip — verified by compiling
`{$IFOPT R+} ... {$ENDIF}` against this build.

## Source inclusion: `{$I file}` / `{$INCLUDE file}`

`{$I file}` and `{$INCLUDE file}` splice a second file's text into the token
stream at the point of the directive, as if it had been typed there by hand.
`fpc -Mtp` accepts the filename with or without surrounding quotes
(`{$I foo.inc}` or `{$I 'foo.inc'}`); both are read here, with no escape
handling inside the quotes.

An absolute path is used as given. A relative path is resolved in this
order:

1. **The including file's own directory** — specifically, whichever buffer is
   *currently* being scanned, not the outermost source file. A nested
   include (`a.pas` includes `b.inc`, which includes `c.inc`) resolves
   `c.inc` relative to `b.inc`'s own directory, not `a.pas`'s, the same
   convention most C-like `#include` implementations use.
2. **Each `-Fi<dir>` in the order given on the command line**, tried only
   after step 1 fails. `-Fi` is a deliberately separate flag and search list
   from `-I` (the ISO 10206 `.pmi` module search path): the two name
   unrelated file kinds, and one dialect's needs should not dictate the
   other's.

A file already open somewhere on the current include stack (`{$I a.inc}`
inside `a.inc` itself, directly or through a longer cycle) is rejected as
`include cycle: '...'` rather than recursing forever. `{$I+}`/`{$I-}` are
*not* this directive — see the IOChecks switch below — told apart from
`{$I file}` purely by whether the argument is exactly `+` or `-` with nothing
else.

### The `{$R+}`-across-`{$I}` position problem

Because `{$I file}` splices text in, the switch table has to get the
*positional* semantics right across the boundary, in both directions: a
switch set before the `{$I}` must still be in force at the top of the
included file, and a switch changed *inside* the included file must still be
in force once control returns to the includer. This is harder than it
sounds because of how source locations are numbered: `SourceManager` lays
buffers out in the order they are *opened*, not the order their text is
read, so an included buffer occupies a numerically *later* stretch of the
shared coordinate space than the file that included it — even though the
include's own last switch point is read *before* the includer's text that
follows the `{$I}` line. The scanner accounts for this by recording an
explicit point at offset zero of every buffer it pushes, and another at the
includer's resume offset when it pops back out (`openInclude`/`popInclude`,
`lib/Lex/Directives.cpp`), turning "what is the state here" back into an
ordinary "last point at or before this offset" query. `SwitchTable::record`
inserts each point in sorted position for exactly this reason — an earlier
version that merely overwrote the last-seen point when a new one didn't sort
after it silently discarded the include's own last state instead, corrupting
any later query for a location inside the include after its own last switch
directive (see `SwitchTable.h`'s comment on `record` for the full mechanism).
`test/Driver/Turbo/` has a dedicated regression for this exact boundary; see
"New test coverage" below.

## `{$R+}`-style switches

Twelve of Turbo's switches are recognized, `include/plang/Basic/CompilerSwitches.def`:

| Switch | Letter | Long name | TP7 default | Acted on? |
|---|---|---|---|---|
| RangeChecks | `R` | `RANGECHECKS` | **off** | Yes |
| IOChecks | `I` | `IOCHECKS` | on | Recorded only (see below) |
| BoolEval | `B` | `BOOLEVAL` | off | Yes |
| ExtendedSyntax | `X` | `EXTENDEDSYNTAX` | on | Yes |
| OverflowChecks | `Q` | `OVERFLOWCHECKS` | off | Recorded only |
| VarStringChecks | `V` | `VARSTRINGCHECKS` | on | Recorded only |
| TypedAddress | `T` | `TYPEDADDRESS` | off | Recorded only |
| OpenStrings | `P` | `OPENSTRINGS` | off | Recorded only |
| StackChecks | `S` | `STACKCHECKS` | on | Recorded only |
| Assertions | `C` | `ASSERTIONS` | on | **Yes** (see below) |
| WritableConst | `J` | `WRITEABLECONST` | on | Recorded only |
| GenerateGoto | (none) | `STACKFRAMES` | off | Recorded only |
| ObjectChecks | (none) | `OBJECTCHECKS` | off | Recorded only |
| Goto | (none) | `GOTO` | on | Recorded only |

A switch is written either by its letter — `{$R+}`/`{$R-}`, matched **only**
when the name is exactly one character immediately followed by `+` or `-`,
no space and no `ON`/`OFF` — or by its long name, which additionally accepts
a space before `ON`/`OFF` (`{$RANGECHECKS ON}`). The letter form is tried
first and, on anything else (a space, a filename, `ON`/`OFF` with no sign),
falls through rather than erroring: real Borland/FPC double up several
letters (`R`, `D`, `F`, `L`, `M`) between a switch and an unrelated named
directive (`{$R resourcefile}` is a Windows-resource directive, not
`RangeChecks`, without the `+`/`-`), told apart the same way. `ObjectChecks`
and `Goto` have no single-letter spelling at all in real Borland/FPC (checked
against `fpc`'s own `switches.pas`, which gives every *other* letter A–Z some
meaning) — `{$O+}`/`{$G+}` are unrelated accept-and-ignore entries (Overlays,
286-instructions), never mistaken for these two just because they share the
long name's first letter. Recognizing a switch produces no warning of its
own either way; only an argument that fails to parse as `+`/`-`/`ON`/`OFF`
falls through to `warning: unknown compiler directive`.

**Acted on** means plang's codegen changes behavior at that switch's state;
**recorded only** means the position-keyed table still remembers the value
precisely so a query can answer truthfully, but nothing currently reads it
to change what gets emitted. Two entries are worth calling out because the
table's own grouping undersells or oversells them:

- **Assertions** (`{$C+}`/`{$C-}`) is grouped with the recorded-only
  switches in the `.def` file's own comments, but is in fact acted on:
  `CGProcCall`'s `Assert(cond[, msg])` lowering reads it directly
  (`RangeCheckGuards::assertionsAt`), and with `{$C-}` in force the whole
  call compiles to nothing — `cond` is never even evaluated. Verified by
  compiling `{$C-}  Assert(false, 'x')` and confirming it exits 0 rather
  than reporting runtime error 227.
- **IOChecks** (`{$I+}`/`{$I-}`) is grouped with the acted-on switches, but
  nothing in codegen queries `Switch::IOChecks` today. It is recorded so
  that a position-aware `{$IFOPT}` (once implemented) and a round-tripped
  `{$I-}` have somewhere to land; the runtime behavior it exists to gate —
  `IOResult` reporting a failed I/O operation instead of the program
  aborting — arrives with a later piece of the file runtime. `{$I-}` compiles
  and is remembered correctly; it does not yet change what a failed
  `read`/`write` does.

Every other "recorded only" switch (`OverflowChecks`, `VarStringChecks`,
`TypedAddress`, `OpenStrings`, `StackChecks`, `WritableConst`,
`GenerateGoto`/`STACKFRAMES`, `ObjectChecks`, `Goto`) is exactly that: parsed,
remembered, and otherwise inert. `OverflowChecks` in particular has a
structural reason to stay that way rather than a temporary one — see
"Documented deviations" below.

### `{$R}` and `-frange-checks` precedence

`-frange-checks`/`-fno-range-checks` on the command line sets
`LangOptions::RangeChecks`, which becomes the *starting* state of the
`RangeChecks` bit before any directive runs (`LangOptions::defaultSwitches()`).
A `{$R+}`/`{$R-}` directive then overrides it **from its own position in the
source forward** — the command line sets where checking starts, a directive
changes where it changes, and a query at any given location gets whichever
of those is the most recent one before it.

The one dialect-specific wrinkle: **Turbo's *recorded* default for
`RangeChecks` is off**, unlike every other switch in the table above, whose
`TurboDefault` column is genuinely just what `{$R+}`/`{$R-}` toggle *from*.
Real Turbo Pascal ships with range checking off; ISO 7185 and Extended
Pascal ship with it on. So `LangOptions::RangeChecks`'s value is dialect-aware
(`true` for ISO 7185/EP, `false` for Turbo) rather than a single fixed
default the way `IOChecks`'s `true` or `BoolEval`'s `false` are for every
dialect alike — and the driver forwards an explicit `-frange-checks` or
`-fno-range-checks` to the front end unconditionally, on every invocation, so
that an explicit `-frange-checks` under `-std=turbo` is distinguishable from
no flag at all and genuinely turns checking back on from the start of the
file (see `test/Driver/Turbo/explicit-frange-checks-through-the-driver-overrides-turbos-off-default.pas`).
A plain `-std=turbo` with no flag and no `{$R+}` anywhere lets an
out-of-range index or subrange assignment through silently, exactly like
real Turbo Pascal.

## Accept-and-ignore directives

Every other real Turbo/Borland/FPC directive this milestone does not act on
is still recognized by name, rather than falling through to "unknown" —
`warning: compiler directive 'NAME' is recognized but has no effect`
(`-Wno-directive-ignored`). The single letters are DOS/Windows/386-target
concerns this project's native LLVM-backed Linux/macOS target has no
analogue for:

| Letter | Real meaning |
|---|---|
| `A` | Data alignment |
| `D` | Debug/description info |
| `E` | Coprocessor emulation |
| `F` | Far calls |
| `G` | 286 instructions / imported data |
| `L` | Object linking |
| `M` | Memory sizing |
| `N` | Numeric coprocessor |
| `O` | Overlays |
| `Y` | Reference/browser info |
| `K` | 8086 smart callbacks |
| `U` | Pentium-safe FDIV |

The long names are newer Delphi/FPC directives with the same shape — real,
but nothing this project's target needs them to mean:

| Long name | Real meaning |
|---|---|
| `APPTYPE` | Application type metadata for the produced executable |
| `CODEPAGE` | Codepage metadata for the produced executable |
| `PACKRECORDS` | Record/field layout hints (see "Documented deviations") |
| `ALIGN` | Field alignment hints (see "Documented deviations") |
| `SMARTLINK` | Linker behavior |
| `WARN` | FPC's own warning vocabulary, distinct from plang's `-W` flags |
| `REGION` / `ENDREGION` | IDE code-folding markers (paired, so opening one is not "unknown") |

## Run-time error codes

Turbo's numbered run-time errors are reported through a parallel reporter
family (`plang_tp_*`, `runtime/plang_sys.cpp`) rather than the shared
ISO/EP `plang_err_*` path: a plang object file compiled `-std=turbo` exits
with the numbered code itself and prints `Runtime error <n> at $<address>`,
matching what a real compiled Turbo/FPC program and script driving it would
show, instead of the one shared exit status (`70`) every ISO/EP check uses
regardless of which one fired.

| Code | Meaning | Raised by |
|---|---|---|
| 0 | (none pending) | `RunError` with no argument and nothing else has failed |
| 106 | Invalid numeric format | `read`/`readln` into a numeric variable: the whole next token failed to parse, or overflowed `int64_t` |
| 200 | Division by zero | `div`, `mod` |
| 201 | Range check error | An out-of-range array index or subrange assignment, under `{$R+}` |
| 215 | Arithmetic overflow error | `div` where the dividend/divisor pair itself overflows (`minint div -1`) — see "Documented deviations" for a current gap |
| 216 | General protection fault | Dereferencing a nil or invalid pointer |
| 227 | Assertion failed | `Assert(false[, msg])`, under `{$C+}` |
| *n* | Whatever the program means by it | `RunError(n)` |

`RunError(code)` (no Sema arm needed, unlike `Assert`) aborts immediately
with any code the program names, including one none of the checks above uses
— the same call real Turbo code uses to signal its own failures.

---

## Documented deviations from real Turbo Pascal / FPC field practice

Everywhere above describes what plang's Turbo mode does; this section is
where it is honest about not matching real Turbo Pascal 7 / `fpc -Mtp`, and
why. Each entry below was checked against this codebase's own comments and,
where practical, against a real compile with this build — not assumed.

### Permanent: real literals are `Double`, not `Extended`

Real `fpc -Mtp` types an unsuffixed real literal as `Extended` — an 80-bit
type with roughly 21 significant decimal digits. plang has only ever had one
floating-point representation, an 8-byte IEEE `Double` (~17 significant
digits), and Turbo's `real` is lowered onto it the same as ISO 7185's and
Extended Pascal's `real` always have been. Turbo's default real-write
*shape* (field width, decimal-place count, three-digit exponent) is
otherwise byte-identical to ISO's own — the two dialects disagree only on
the exponent letter's case (`E` vs `e`, `PlangRealProfileTurbo` vs
`PlangRealProfileISO`, `runtime/plang_real.h`) — but the *values* a real
Turbo binary would print for anything near the edge of `Double`'s precision
cannot be reproduced here, and no golden output for a Turbo real-formatting
test can ever be transcribed directly from a real `fpc -Mtp` run. Every real
number in this project's Turbo test suite derives its expected output from
plang's own `Double`-based profile, not from a real fpc transcript.

### Stack overflow (run-time error 202) is not detected

Real Turbo/FPC reports "Runtime error 202: Stack overflow error" when a
program's call stack grows past a set limit — typically from unbounded
recursion. plang has no equivalent runtime guard: there is a *compile-time*
limit on how deeply one **expression** may nest (`MaxExprDepth`,
`lib/CodeGen/CGExprCore.h`), defensive against `CodeGen`'s own recursive
descent through a single expression tree, but nothing measures or bounds the
Pascal-level **call stack** at run time. A genuinely unbounded recursive
procedure call chain runs until the OS stack is exhausted and the process
receives a raw `SIGSEGV`, not a clean "Runtime error 202" — under `-std=turbo`
exactly as under every other dialect.

### `RunError`/RTE 215 doesn't fire for a 16-bit `Integer`'s true overflow pair

`minint div -1` is the one `div` with a nonzero divisor that still has no
representable result, since `-minint` doesn't fit a positive integer of the
same width — real Turbo Pascal reports this as runtime error 215. The guard
that checks for it (`RangeCheckGuards::emitDivOverflowCheck`) takes the
width to check *at* as a parameter specifically so it can compare against
the right `minint` — but `CGBinaryOps`' `Div` case widens both operands to
`i64` (`ToI64`) *before* calling it, and calls it with no `Width` argument,
which defaults to 64. So the guard correctly never fires for a genuine
64-bit overflow, but a 16-bit Turbo `Integer`'s own overflow pair
(`-32768 div -1`) is invisible by the time the check runs: sign-extended to
`i64`, neither operand looks like a 64-bit `minint`/`-1` pair, and `SDiv`
silently computes a wrapped result instead of trapping. Confirmed against
this build:

```pascal
program divovf;
var n, d, r: integer;   { Turbo Integer: 16-bit }
begin
  n := -32768; d := -1;
  r := n div d;
  writeln(r)             { prints -32768, exit 0 -- no Runtime error 215 }
end.
```

This is a narrow, known gap in the current codegen, not a design decision —
unlike the permanent divergences above, a future change to pass the
operation's real width through could close it.

### `packed` controls layout; `{$PACKRECORDS}`/`{$ALIGN}`/`{$A}` do not

A `packed record` is lowered to a genuinely packed (byte-aligned) LLVM
struct in every dialect, Turbo included, and an unpacked one uses ordinary
struct alignment. Real Borland/FPC additionally lets `{$PACKRECORDS n}` (and
the older `{$A n}`/`{$ALIGN n}`) pick an *intermediate* alignment — pack
fields to an `n`-byte boundary without going all the way to 1 — which is
genuine, observable layout control real Turbo/FPC source relies on for a
binary record image. Both directives are in plang's accept-and-ignore table:
recognized, warned as having no effect, and never translated into any actual
alignment. A real Turbo program that depends on a specific `{$PACKRECORDS}`
setting for its record layout will not get that same layout from plang —
only the plain `packed`/unpacked distinction is honored.

### Not a Turbo deviation (checked and excluded)

This project's own history has a `div`/`mod` sign-handling fix connected to
issues #228 and #394 ("extent-form mod uses ISO sign, not raw `srem`"/"not C
truncation"). That fix is about how array-extent arithmetic is computed
internally for schema debug info (the `gdb` pretty-printer), under ISO 7185
and Extended Pascal — it has nothing to do with Turbo's `div`/`mod`, and
predates this milestone. It does not belong in this list, and is called out
here only so it is not mistaken for one on a future read of the git log.
Turbo's own `mod` sign rule (it takes the dividend's sign, plain `srem`, not
ISO §6.7.2.2's `0 <= mod < divisor` normalization) is a deliberate,
documented *reversal* from ISO, confirmed against `fpc -Mtp`, not a
deviation from Turbo/FPC practice — see `RangeCheckGuards::emitModDivisorCheck`'s
own comment.

---

## See also

- `plang(1)` — the full command-line reference, including `-d`/`-u`/`-Fi`/`-frange-checks`.
- [`docs/conformance.md`](conformance.md) — the ISO 7185 clause 5.1 documentation for the base language.
- [`docs/modules.md`](modules.md) — Extended Pascal modules and separate compilation.
- `include/plang/Basic/CompilerSwitches.def`, `lib/Lex/Directives.cpp` — the source of truth this document was written from.
