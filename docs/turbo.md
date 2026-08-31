# Turbo Pascal in plang

`-std=turbo` selects Turbo Pascal 7 as plang reads it: the dialect's own
16-bit signed `Integer`, its keywords and operators (`@`, `shl`/`shr`, bitwise
`and`/`or`/`not`/`xor`, `Exit`/`Break`/`Continue`), its literal syntax
(`$hex`, `#code`, `^ctrl`, adjacent string/char gluing), its `case`-statement
and text-formatting rules, and — the subject of this document — a `{$...}`
compiler-directive system with no equivalent in ISO 7185 or Extended Pascal.
This was Tier 1 of the Turbo milestone: enough of the dialect to compile and
run real Turbo/FPC-`-Mtp` source. Tier 2 (this document's later sections)
builds the type system real Turbo programs actually declare things with —
the sized-integer ladder, `PChar` and pointer arithmetic, procedural types
and values, typed constants and `absolute`, the Boolean-family variants and
`Single`, `string[N]` (`ShortString`) with its full TP value semantics, the
System-unit string routines, and `const`/untyped/open-array parameters.
Tier 3 (also this document's later sections) is the System-unit file
runtime (`Assign`/`Reset`/`Rewrite`/`Append`/`Close`, `InOutRes`/
`IOResult`/`{$I+}`/`{$I-}`, `BlockRead`/`BlockWrite`/`Seek`/...) and
`Random`. Tier 4 is units — `uses`, TP's own scoping rules, real separate
compilation, and the shipped `Crt`/`Dos`/`Printer`/`Strings` standard
library. Tier 5 (this document's own "Object types" section, near the end)
is TP7's own `object` model — inheritance, virtual methods and the VMT,
constructors/destructors, `with`, and visibility (Cluster A); an object
type declared in one separately-compiled unit, inherited from and
overridden in a second, and used from a third (Cluster B) — a real
cross-unit ancestor chain, `.tui`-serialized private fields included, with
virtual dispatch through an ancestor-typed pointer still reaching the
correct override across the unit boundary; and a capstone integration test
corpus at the scale of Tier 4's own, plus a handful of known gaps this
capstone's own testing found and pinned rather than fixed (Cluster C) —
the whole tier is now complete. Still not (yet) covered, at any tier: the
real-mode DOS surface (`Seg`/`Ofs`/`Mem`/`Intr`/...), which plang rejects
by name as targeting a machine this compiler does not build for.

This document covers what `plang(1)` doesn't have room for: every accepted
`{$...}` directive, the predefined `{$IFDEF}` symbols, how `{$I file}`
resolves a path, the full shape of every Tier 2 type and routine below, and —
because a compatibility dialect is only honest if it says where it stops
being one — every point where plang's Turbo mode is known to diverge from
what a real `fpc -Mtp` (this project's stand-in for Turbo Pascal 7 itself,
and empirically checked against throughout) actually does. Tiers 1-3's
divergences are collected in one place, "Documented deviations," near the
end; Tier 4's own one open gap (unit initialization sections) is
documented in its own section instead, since — unlike the others — it is
not a divergence checked against real `fpc -Mtp` field practice so much as
a still-unscoped piece of this tier's own work.

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
| IOChecks | `I` | `IOCHECKS` | on | **Yes** (see below) |
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
to change what gets emitted. Two entries are worth a closer look:

- **Assertions** (`{$C+}`/`{$C-}`) is grouped with the recorded-only
  switches in the `.def` file's own comments, but is in fact acted on:
  `CGProcCall`'s `Assert(cond[, msg])` lowering reads it directly
  (`RangeCheckGuards::assertionsAt`), and with `{$C-}` in force the whole
  call compiles to nothing — `cond` is never even evaluated. Verified by
  compiling `{$C-}  Assert(false, 'x')` and confirming it exits 0 rather
  than reporting runtime error 227.
- **IOChecks** (`{$I+}`/`{$I-}`) genuinely is acted on:
  `RangeCheckGuards::ioChecksAt`/`CGProcCall::emitIoCheckIfNeeded` read it
  directly at every checked I/O statement's own source position, emitting
  the automatic `plang_iocheck()` abort under `{$I+}` and suppressing it
  under `{$I-}` — Tier 3's own `InOutRes`/`IOResult` contract ("Tier 3"
  below) is exactly this switch's runtime behavior landing. (Earlier in
  this milestone, before Tier 3 shipped, `{$I+}`/`{$I-}` compiled and was
  remembered correctly but changed nothing at runtime; that interim state
  is gone as of Tier 3.)

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

# Tier 2: types, values, and the System-unit string routines

Tier 1 (above) is the dialect's syntax and its compiler-directive system.
Tier 2 is the type system a real Turbo program actually declares variables,
parameters, and constants with: a ladder of sized integers, a real pointer
type with arithmetic, procedural values, static-storage "typed constants,"
loose Booleans, a 32-bit float, a genuinely different bounded-string type
from Extended Pascal's, and the handful of System-unit routines that make a
bounded string usable (`Copy`, `Pos`, `Delete`, `Insert`, ...). Every feature
below is gated to `-std=turbo` and rejected under `-std=iso7185`/
`-std=iso10206` exactly like Tier 1's own extensions.

## The sized-integer ladder

`-std=turbo` declares nine additional integer names, `AnsiChar`, and an
untyped `Pointer`, alongside the dialect's own 16-bit `Integer`:

| Name | Width | Signed | Real TP7, or later? |
|---|---|---|---|
| `ShortInt` | 8 | yes | genuine TP7 |
| `Byte` | 8 | no | genuine TP7 |
| `SmallInt` | 16 | yes | **FPC** (an explicit synonym for `Integer`; not in TP7 at all) |
| `Word` | 16 | no | genuine TP7 |
| `Integer` | 16 | yes | genuine TP7 (the dialect's own plain integer, unaffected) |
| `LongInt` | 32 | yes | genuine TP7 |
| `Cardinal` | 32 | no | **FPC/Delphi** (TP7 had no unsigned 32-bit type) |
| `LongWord` | 32 | no | **FPC** (an explicit synonym for `Cardinal`) |
| `Int64` | 64 | yes | **FPC/Delphi** (TP7's widest integer was `LongInt`, 32 bits) |
| `QWord` | 64 | no | **FPC** |
| `AnsiChar` | 8 | (char) | **Delphi/FPC** naming for what TP7 just called `Char` |
| `Pointer` | (target width) | (pointer) | genuine TP7, untyped, assignment/comparison-compatible with any other pointer type in either direction |

This isn't a gap being closed reluctantly — it's this project's own "match
field compilers on ambiguity" convention (cited by that name in, e.g.,
`CGFuncCall.cpp`'s own comment on `Hi`/`Lo`/`Swap`, below): where real TP7
and `fpc -Mtp` disagree, plang follows `fpc -Mtp`, since that is the actual,
checkable, still-maintained target this whole dialect is verified against,
not a museum-piece compiler nobody can run anymore. `SmallInt`/`Integer` and
`LongWord`/`Cardinal` are not merely
compatible — they are **the same interned `Type` object**
(`TypeContext::getInt` keys purely on `{Width, Signed}`), so a variable
declared `SmallInt` and one declared `Integer` are assignment-compatible for
the ordinary reason two variables of one type always are, and a diagnostic
about either says whichever name is idiomatic: `getInt` names a
`{Width, Signed}` pair with the ladder's own name (`ShortInt`, `Byte`, ...)
*unless* it is exactly the dialect's own unqualified `integer` (16-bit
signed), which keeps that plain lowercase name. So an error about a 32-bit
unsigned value always says `'Cardinal'`, never `'LongWord'`, even if the
program itself wrote `LongWord` — an accurate description of the same type
either name denotes, not a bug in which spelling "won."

None of these eleven names exist under `-std=iso7185` or `-std=iso10206`;
ISO 7185/Extended Pascal keep their own single, always-64-bit `Integer` and
`Real` exactly as before Tier 2, unaffected by construction (every new type
is minted only when `Opts.turbo()` is true).

### Narrowed subrange and enum storage (TP7 ch.19)

A numeric subrange under `-std=turbo` is stored at the *narrowest* width
that holds its own declared bounds, not at its host type's width — `type
Grade = 1..100` is a **byte**, even though `1` and `100` are themselves
written as plain `Integer` (16-bit) literals. This is genuine TP7 chapter 19
field practice, verified against a real `fpc -Mtp`: `1..100` is 1 byte;
`-100..100` is 1 byte *signed* (the negative bound needs the sign bit even
though `100` alone would not); `-200..200` is 2 bytes signed (`-200` doesn't
fit signed 8-bit); `0..1000000` is 4 bytes; `0..255` is 1 byte *unsigned*;
`0..256` is 2 bytes. The exact rule
(`TypeContext::narrowestStorage`) tries unsigned 8/16/32-bit first (in that
order, whenever the lower bound is `>= 0`), then signed 8/16/32-bit, and
falls back to 64-bit signed only if nothing narrower fits — so a subrange
with a nonnegative lower bound is *always* unsigned at whatever width it
lands on, never signed at that same width, even when a signed encoding of
that width would also have held it. An enumeration's own implicit `0..N-1`
range is narrowed by the identical rule and the identical helper (a
3-member enum is a byte; a 300-member one is a `Word`). A subrange hosted on
`Char` or `Boolean` gets its host's own natural width (8 bits) instead of
falling back to the dialect's default width, and a subrange hosted on an
already-narrowed `Enum` inherits that enum's own (possibly narrowed) width.
ISO 7185 and Extended Pascal are completely unaffected — this narrowing is
gated on the same `Turbo_` flag as everything else in this section, and
every dialect other than Turbo keeps the wide, uniform storage it always
had.

### Value and variable typecasts

`TypeName(expr)` is a typecast, not a function call, resolved by the same
mechanism EP's own `TypeName[...]` bracket-cast machinery already uses,
extended to Turbo's parenthesized spelling. It means two different things
depending on whether it appears where a *value* is wanted or where a
*variable* (an lvalue) is:

- **As a value** — `Integer(SomeReal)`, `Byte(SomeWord)` — it *converts*:
  ordinal-to-ordinal reinterprets the ordinal value (truncating or
  sign/zero-extending as needed, the same as an assignment between the two
  types would), and real-to-ordinal truncates toward zero (`Trunc`'s own
  rule), never rounds.
- **As a variable** — `TByteRec(SomeWord).Lo := 0`, or a bare
  `Color(c) := Red` as a whole assignment target — it does **not** convert
  anything. It reinterprets the operand's *own storage* in place, the C
  `reinterpret_cast`/union-overlay idiom real Turbo code uses to pick apart
  a wider value's bytes. This requires the two types be *exactly* the same
  size (`Sema::byteSizeOf`, the identical size arithmetic `SizeOf` itself
  answers from — see below), or it's a compile-time error: `Word(SomeByte)`
  as an lvalue is rejected, since a two-byte reinterpretation of one-byte
  storage would read past the end of it.

A variable typecast is its own AST node kind (`TypeCastExpr`), not a
`CallExpr` with a type-named callee, specifically so it can be an lvalue:
the ordinary call-expression code path spills its result to a temporary,
which would silently turn `TByteRec(w).Lo := 0` into a write to a throwaway
copy instead of mutating `w`'s real storage.

## `PChar`/`PAnsiChar`, pointer arithmetic, and `p[i]` indexing

`PChar` (and its FPC-era synonym `PAnsiChar`) is Turbo's null-terminated
C-style string pointer: `p + n` and `p - n` (pointer arithmetic), `p1 - p2`
(the element count between two pointers of the same pointee type), `p[i]`
indexing as both a read and a write, and a zero-based `array[0..n] of Char`
decaying to its own address with no `@` needed (`p := buf`) all work on it.

**The gate is structural, not nominal — this was corrected empirically
against a real `fpc -Mtp` (3.2.2) during this project's own Turbo work,
after an initial draft gated on "is this literally the predefined `PChar`
type."** Real Turbo/Delphi/FPC gives this same arithmetic to *any* pointer
whose pointee is `Char` — a program's own `type P = ^Char; var q: P;`
declaration gets exactly the same `q + 1`, `q[i]`, and array-decay treatment
`PChar` does, confirmed across both `{$mode tp}` and `{$mode objfpc}`, with
`{$pointermath off}` and `{$T+}` both forced explicitly to rule either one
out as the real gate. A `^Byte` or `^Integer` alias does **not** get this
treatment merely for sharing a width with `Char` — the rule keys on the
*pointee type*, not the pointer's declared name or its underlying
representation. plang's own gate (`isCharPointerType`, `Type.h`) matches
this exactly: any pointer type whose pointee resolves to `Char` qualifies,
checked independently at each of the three call sites that need it
(`Sema::checkBinary` for `+`/`-`, `Sema::checkIndex` for `p[i]`,
`Sema::isAssignCompatible` for the array-decay assignment) — not a special
case for the name `PChar` anywhere in Sema.

Two scope boundaries worth being explicit about:

- **ISO 7185 and Extended Pascal `^char` are completely untouched.** Their
  `=`/`<>`-only comparison (ISO §6.7.2.5) is exactly what it always was;
  every one of the checks above is additionally gated on `Opts.turbo()`,
  independent of the structural pointee check.
- **Only a *zero-based* `array[0..n] of Char` decays.** ISO §6.4.3.2's
  canonical `array[1..n] of char` string-type — 1-based — does **not**
  decay to a `PChar`-compatible pointer, matching `fpc -Mtp`'s own refusal
  of the 1-based case. This is not an oversight; a 1-based array's element
  0 doesn't exist, so decaying it to a pointer whose arithmetic assumes
  index 0 is the start would be silently wrong.

`write`/`writeln` of a `PChar` value prints the bytes it points to up to the
terminating NUL (gated to `-std=turbo`, confirmed against `fpc -Mtp`).
`read`/`readln` of a `PChar` remains rejected — also matching `fpc -Mtp`: a
bare pointer carries no buffer capacity for a reader to respect, so there is
no safe destination to read into.

## Procedural types and procedural variables

`type TProc = procedure(x: Integer);` declares a procedural type, and `f:
TProc` a *variable* of it — genuinely new surface with no ISO 7185/Extended
Pascal equivalent (the existing procedural/functional *parameter* form, ISO
§6.6.3.1, is unaffected and untouched by this feature). `f := SomeRoutine`
and `f := @SomeRoutine` both assign a reference (the general `@` operator
from Tier 1, optional here); `f(10)` calls through it, dispatching to
whichever routine was assigned most recently.

**Disambiguating `f := g`** — "call `g` and assign its result" vs. "take a
reference to `g`" — is resolved by the **assignment target's own type**:
only when the target's resolved type is itself callable (`Procedure`/
`Function`) does a bare routine name on the right-hand side become a
reference. `f: Integer; f := SomeFunc` still means exactly what it always
has under every dialect — call `SomeFunc`, assign its result — since
`Integer` is never itself callable. This is the single most important
compatibility property of the feature: it must not disturb the ubiquitous
"assign a function's own result via its own name" idiom that appears
throughout ordinary ISO/EP/Turbo code.

**The nested-routine restriction.** A procedural *variable* lowers to one
flat function pointer, unlike a procedural *parameter*, which is carried as
an `{entry point, frame}` pair (`ClosureAndCallABI`). Storing a *nested*
routine's reference into a flat pointer would silently drop its static
link — the moment the enclosing activation returns, that pointer dangles,
a memory-corruption bug rather than a mere type error. So `checkRoutineValue`
refuses to assign either of two things to a procedural variable:

1. **A textually nested routine**, by name (`Symbol::IsNested`) — refused
   outright, not only when it is proven to actually capture an outer
   variable, since whether it does is not something this check tries to
   prove either way.
2. **A procedural parameter itself** — its own actual binding (possibly to
   some other, nested, capturing routine from an entirely different
   activation) isn't known at the point it would be assigned, so it is
   refused the same way.

A **top-level** (non-nested) routine has no static link to drop and may
always be assigned. A procedural *variable* may itself be captured by a
nested routine through the ordinary static-link mechanism, like any other
outer variable — what's disallowed is only storing a nested routine's own
reference *into* one, not reading one from within nested code.

## Typed constants and `absolute`

**Typed constants** (`const X: Integer = 0;`) are not constants at all —
matching real TP7, plang gives one static storage and a one-time
initializer, and it may be assigned to like an ordinary variable
afterward. Implemented as `SymbolKind::Var` (with `Symbol::IsTypedConst`
set), deliberately never `SymbolKind::Const` — which is what makes it
correctly refused as an array bound or `case` label with no extra rejection
logic of its own: `Sema::constBound` only ever folds a real
`SymbolKind::Const`.

Declared **inside a procedure**, a typed constant keeps its value across
separate calls, the same as a C `static` local — not a fresh,
freshly-initialized stack slot on every activation the way an ordinary
local `var` or local `const` gets. It has its own internal-linkage
`llvm::GlobalVariable`, mangled with the enclosing procedure's own scope, in
place of the per-activation alloca every other local receives.

The initializer must fold to a genuine compile-time constant (TP7's own
rule — it's baked into the program's data segment once, not computed at
start-up); a scalar, or a *fixed* (non-variant) record/array built from
scalars, folds to a real `llvm::Constant` aggregate, with Turbo's own
positional literal syntax (`(1, 2, 3)`, or `(X: 10; Y: 20)` for a record —
unlike EP's labeled `[1: 1; 2: 2]` bracket form). `Set`/`String`/`File`/
`Pointer`/`Procedure`/`SchemaInstance`-typed constants and variant records
are refused with a clear diagnostic rather than partially or unsoundly
handled — a scope line for this first implementation, not a permanent
restriction.

**`absolute`** (`var W: Word absolute B;`) overlays a new variable's
storage directly onto an existing one's, rather than allocating storage of
its own — `W` and `B` genuinely share memory from then on, not merely start
out equal. It's not a reserved word: recognized only by spelling, only
immediately after a var-declaration's type, so a program's own unrelated use
of the identifier `absolute` anywhere else is completely unaffected. A
local `absolute` (declared inside a procedure) can overlay any addressable
designator, including a component (`absolute B[1]`, `absolute SomeRec.Field`)
— it runs with a real entry block already open, so the ordinary lvalue
machinery can address it. A **global** `absolute` currently supports only a
bare variable name as its target, a narrower scope than the local case (not
a permanent restriction, just what this item's first landing covers). Both
directives are `-std=turbo`-only, rejected as an ordinary syntax error under
every other dialect.

## Boolean-family variants and `Single`

**`ByteBool`/`WordBool`/`LongBool`** (8/16/32 bits) are Turbo's "loose"
Booleans: unlike strict `boolean` (always exactly `{0, 1}`, still lowered to
`i1` and unaffected by any of this), a loose Boolean may legally hold *any*
bit pattern, and **any nonzero value reads as true** — `ByteBool(200)` is
stored as the literal byte 200 and tests true, not silently normalized to
1. This reuses `TypeKind::Boolean` with a new `Type::IsLooseBool` flag,
rather than a new `TypeKind` — the same shape the sized-integer ladder
already uses to add width without adding new kinds. `set of ByteBool` and
`array[ByteBool]` are both refused, matching `fpc -Mtp`: a loose Boolean is
treated as unbounded for exactly the same reason an ordinary `Integer`
would be, since checking a whole loose-Boolean-sized range as a set/index
base is not what any real program means by declaring one. The only way to
*construct* a loose Boolean's own non-canonical value is a typecast
(`ByteBool(200)`) — assigning a plain integer to one is itself rejected, so
a working cast is not merely a convenience here.

**`Single`** is a second, 32-bit `Real` (`TypeKind::Real` with `Width = 32`,
lowered to LLVM `float` rather than `double`) alongside the dialects'
existing 64-bit `Real`. Its default (no field-width) `write`/`writeln`, and
a width-only (no decimals) field write, both cap output at nine significant
digits and pad with leading spaces rather than showing double-precision
noise past what a 32-bit float can actually represent — see "Documented
deviations" for the related, permanent real-literal-width caveat this does
not (and cannot) resolve.

**`Extended`/`Comp`** — real TP7's 80-bit and 64-bit-integer-backed
numeric types — get an explicit, dialect-aware diagnostic under
`-std=turbo` naming them as unsupported, rather than the generic "undefined
type" a typo would produce elsewhere; outside Turbo they remain simply
undefined, exactly as before this item. Neither is implemented; see
"Documented deviations" for why `Extended` specifically has no path to one
(this project has only ever had one floating-point representation, 8-byte
`Double`).

## `ShortString`: Turbo's `string[N]`, contrasted with EP's `string(N)`

**This is the single most important thing to keep straight when reading
Turbo source alongside Extended Pascal source: `string[N]` and `string(N)`
look similar and are not.** They are two different `TypeKind`s
(`ShortString` vs. `VarString`), two different binary layouts, and — the
point of this section — different value semantics at nearly every operation
that touches one. Turbo's `-std=turbo` uses square brackets; EP's
`-std=iso10206` uses parentheses; the two dialects are structurally mutually
exclusive today, so this contrast is never something a single program has
to navigate, but a reader moving between plang's own two Pascal dialects
does.

### Layout

| | `string[N]` (Turbo `ShortString`) | `string(N)` (EP `VarString`) |
|---|---|---|
| Length field | 1 byte, `0..255` | 8 bytes (`i64`) |
| `SizeOf` | `N + 1` exactly, no padding | `roundUp(8 + N, 8)` |
| Struct | `<{i8, [N x i8]}>`, **packed** (1-byte aligned throughout) | `{i64, [N x i8]}`, normally aligned |
| Bare `string` (no size) | Capacity **255** (confirmed real TP7/FPC field practice — not EP's unbounded `String`) | N/A — EP always requires an explicit or discriminant-derived capacity |
| Maximum declarable capacity | Length field is 1 byte wide, so no *in-bounds* length can ever exceed 255 regardless of `N`'s own declared size | Governed only by the 1 GiB declaration-size gate |

`SizeOf(string[10])` is `11`; a record with two `string[10]` fields is `22`
bytes (both fields at 1-byte alignment, no padding between or after them,
since nothing in a `ShortString` is ever wider than a byte). This is a
*completely different, incompatible* runtime (`runtime/plang_sstr.cpp`) from
EP's `string(N)` (`runtime/plang_str.cpp`) — no function in one is ever
called with the other's pointer, and `StringCallMarshalling::emitCallArg`
picks between them by checking the parameter type's actual packedness, not
by LLVM struct shape (both are superficially "a two-element struct with an
`[N x i8]` second element," which is exactly the mismatch a shape-only test
would miss — and did, transiently, during this feature's own development;
now dispatched on `paramTy->isPacked()`).

### Assignment: truncates, never errors

A `string[N]` assignment longer than `N` **silently truncates** — matching
real Turbo/FPC field practice. This is the opposite of EP's `string(N)`,
where ISO 10206 §6.9.2.2 makes an over-capacity assignment a runtime error
(`plang_err_str_capacity`, "assigned to a string(N)"). Every `ShortString`
runtime function that writes a result clamps to the destination's declared
(255-ceilinged) capacity instead of calling any capacity-error reporter.

### Comparison: prefix order, shorter is less — the opposite of EP

A `ShortString` compares by **prefix lexicographic order with the shorter
string treated as less** whenever one is a strict prefix of the other:
`'a' < 'a '` is **true**. EP's `string(N)` instead **space-pads the shorter
operand out to the longer one's length before comparing**
(`plang_str.cpp`'s `strCmp`), so under EP `'a' = 'a '` is **true** — the
same two literals, opposite relational answer, for opposite reasons (one
treats the trailing space as real content that makes the values unequal and
resolves the tie by length; the other treats it as padding that makes them
equal). Neither implementation shares any comparison code with the other —
`plang_sstr.cpp`'s `sstrCmp` compares only the overlapping prefix and then
breaks a length tie directly, with no padding step at all.

### `s[0]` is the length byte — a `0..capacity` indexing range, not EP's `1..length`

Indexing a `ShortString` at `0` reads (and, as an lvalue, writes) its own
one-byte length field directly — `Ord(s[0]) = Length(s)` always holds, and
remains true after any mutation (`Insert`/`Delete`/`SetLength`/plain
reassignment), since every one of those routines updates the same length
byte `s[0]` aliases rather than some separate bookkeeping field. This has no
EP equivalent: EP's `string(N)` indexes `1..length` only, with its own
8-byte length header entirely inaccessible through ordinary indexing.

### Concatenation, function results, and parameters

`+` concatenation clamps at the destination's declared (or 255, for a bare
`string`) capacity, the same silent-truncate rule assignment follows,
implemented by `plang_sstr_concat` and friends — a genuinely different
runtime family from EP's own `+`. A `ShortString` function result and a
`ShortString` value parameter are both copied/spilled at the **callee's own
declared width**, not the caller's — a confirmed, fixed bug in this
feature's own early development (`StringCallMarshalling::emitCallArg` was
picking the wrong runtime family by LLVM shape alone, described above) is
what makes this guarantee worth stating explicitly rather than assuming it
falls out of the general calling convention for free.

## The System-unit string routines

`Copy`, `Pos`, `Concat`, `Delete`, `Insert`, `SetLength`, `StringOfChar`,
`UpCase`, `Str`, and `Val` are Turbo's own String-routine surface, all
`-std=turbo`-only, all operating on `ShortString`s specifically — genuinely
new runtime entry points (`plang_sstr_*`), never a reuse of EP's
`plang_str_index`/`plang_str_substr`/etc., because several of them
*deliberately* disagree with EP's superficially similar functions on the
exact question that matters:

| Routine | Signature | Key semantics |
|---|---|---|
| `Copy(s, index, count)` | function, returns capacity-255 `ShortString` | **Clamps** `index`/`count` into range rather than raising (EP's `substr` raises out of range). `index < 1` clamps to `1` **without re-basing `count`** off the clamp — `Copy(s, 0, 5)` and `Copy(s, 1, 5)` give the same five characters. `count < 0` clamps to `0`. An index past the source's own length yields an empty result. |
| `Pos(pat, s)` | function, returns `Integer` | 1-based index of the first match, `0` if none. **`Pos('', s)` is `0`** for every `s` — the *opposite* of EP's `index('', s) = 1` (ISO 10206's own rule). A wholly separate function from EP's `index` for exactly this reason, even though the underlying scan is the same naive substring search. |
| `Concat(s1[, s2, ..., sn])` | function, variadic (1 or more args), returns capacity-255 `ShortString` | Same clamped concatenation `+` already implements, chained; needs no dedicated runtime entry point. |
| `Delete(var s, index, count)` | procedure, mutates `s` in place | An out-of-range `index` (`< 1` or `> Length(s)`) makes the **whole call a no-op** — unlike `Copy`, nothing is clamped. `count` itself is still clamped to what's actually available (and to `>= 0`). |
| `Insert(source, var s, index)` | procedure, mutates `s` in place | Unlike `Delete`, an out-of-range `index` **is** clamped (to `1`, or to `Length(s)+1`) rather than making the call a no-op. Clamped at `s`'s own declared capacity; built through a 255-byte scratch buffer since source and the displaced tail can overlap the destination in ways one `memmove` can't express. |
| `SetLength(var s, newLength)` | procedure, mutates `s` in place | Sets `s`'s length byte directly, clamped to `[0, s`'s declared capacity`]`. Bytes newly exposed by growing are left as whatever they already held — no zero-fill, no space-padding (confirmed: real `fpc -Mtp` doesn't touch them either). **Two safety divergences from real `fpc -Mtp`, both deliberate** — see "Documented deviations." |
| `StringOfChar(ch, count)` | function, returns capacity-255 `ShortString` | `count` copies of `ch`, clamped at 255 (`StringOfChar('x', 300)` is 255 `'x'`s, not a runtime error). |
| `UpCase(ch: Char): Char` | function | Takes and returns a **`Char`**, not a string — the two-argument-shape overload is a later Delphi/`SysUtils` addition this milestone deliberately excludes. Only `'a'..'z'` change; every other byte, ASCII or not, passes through unchanged. |
| `Str(x[:w[:d]], var s)` | procedure | Formats `x` exactly the way `write(x[:w[:d]])` already does (reuses the `writestr` capture machinery), into `ShortString` destination `s`. |
| `Val(s, var v, var code)` | procedure | See below — Turbo's one **non-fatal** numeric-parsing entry point. |

`Length(s)` itself, previously EP-only, is now available under `-std=turbo`
too, reading a `ShortString`'s own length byte the same way `s[0]` does.
`LowerCase`/`UpperCase` and `Trim`/`Index`/`Substr` are **deliberately not**
part of this surface: the first pair is a later `SysUtils`-era addition
absent from real TP7's System unit; the second three already exist as
EP-only builtins with EP-specific semantics (`Index('', s) = 1`, `Substr`
raising out of range) that must not be disturbed by, or reused for,
anything Turbo-shaped.

### `Val`'s error contract

Every *other* numeric-parsing entry point in this runtime
(`plang_read_i64`/`_f64` and friends) is fatal on malformed input — it
reports and exits the process. `Val` is the opposite, and is the entire
reason it's a new, separate, non-fatal primitive
(`runtime/plang_val.cpp`) rather than a thin wrapper over `read`'s existing
scanners: `code` is an **output parameter**, `0` on success or the
**1-based index of the first character that doesn't fit `Val`'s grammar**
on failure, and control **always** returns to the caller either way, never
a process exit.

- **Integer form**: optional leading blanks, an optional `+`/`-` sign
  (`IsUnsigned` destinations reject a leading `-` outright, `Code = 2`, even
  for magnitude 0 — `Val('-0', aWordVar, code)` fails exactly like
  `Val('-1', ...)` does), an optional radix prefix (`$` or `x`/`X` for hex —
  `'x12'` and `'0x12'` both mean hex 18; `&` for octal; `%` for binary),
  then one or more digits of that radix. The **entire remainder must be
  consumed** for success (`'123  '` with trailing spaces fails at the first
  trailing space, not silently accepted). On success, the full `int64_t`
  magnitude (sign applied) is written to the output; **overflow of `int64_t`
  itself** — not the eventual destination's own, possibly narrower, width —
  is what `Code` reports as failure (a 21-digit literal fails at digit 19,
  exactly where the accumulator overflows). A value that overflows the
  *destination's* width but still fits `int64_t` is instead a `Code = 0`
  success whose value simply doesn't fit — the caller truncates it into the
  destination with an ordinary sign-extend-or-truncate, reproducing `fpc`'s
  own silent wraparound exactly (`Val('40000', aTurboIntegerVar, code)` →
  `code = 0`, value wrapped to `-25536`).
- **Real form**: optional leading blanks, optional sign, decimal digits, an
  optional `.`-fraction, an optional `e`/`E` exponent (sign optional,
  **digits required** once `e`/`E` appears at all) — same "entire remainder
  must be consumed" rule. **No radix-prefix extension** — `Val('$FF',
  aRealVar, code)` fails at position 1; Turbo's hex/octal/binary literal
  forms are integer-only.
- **Two deliberate, documented divergences from `fpc -Mtp`'s own exact
  behavior**, both still fully within `Val`'s contract (non-fatal, a
  defensible `Code` on failure): real `fpc -Mtp`'s real-number scanner has
  an internal backtracking quirk around a trailing `e`/`e+`/`e-` with no
  exponent digits that could not be reduced to one general rule from the
  cases tried — `Val('1e', ...)` fails at `Length+1`, but `Val('1e+', ...)`
  **succeeds**, silently discarding the `e+`, inconsistent even within
  `fpc` itself. plang's single-pass, non-backtracking scanner instead
  uniformly fails both cases at the position right after the introducer.

## `const`, untyped, and open-array parameters

Three related Turbo parameter forms, all `-std=turbo`-only:

- **`const` parameters** (`procedure P(const x: T)`) are read-only —
  assigning to `x` is `err_const_param_assigned` — but are a *distinct*
  mechanism from EP's protected value parameter, with a distinct
  diagnostic, because the two differ in calling convention: a **structured**
  `const` actual (record/array/set) is passed **by reference**, for the
  efficiency the feature exists to provide, confirmed via IR inspection to
  use a bare pointer rather than copying the whole value the way a plain or
  EP-protected value parameter still does.
- **Untyped parameters** (`procedure P(var x)` — no `: type` at all) are
  the classic `memcpy`/`memcmp`-idiom form, confirmed against `fpc -Mtp`
  that only the `var` spelling is legal (no untyped value-parameter form
  exists). Sema rejects every use of `x` except as the operand of a
  **variable typecast** (`Integer(x) := 0`) or a **direct relay to another
  untyped formal** (`procedure Q(var y); begin Q(x) end` where `x` is
  itself untyped) — both confirmed against `fpc -Mtp` as the only legal
  uses — with a real diagnostic (`err_untyped_param_bare_use`) rather than
  a crash or a silent fallback.
- **Open-array parameters** (`procedure Sum(a: array of Integer): Integer`)
  are Turbo's own syntax, distinct from EP/ISO 7185 Level 1's
  conformant-array-schema form (`array [lo..hi: T] of E`) — the two stay
  mutually rejected under each other's dialect. Reuses the existing
  conformant-array machinery (ptr+bounds calling convention, by-value
  copy-on-modify) rather than a new mechanism: the callee always sees
  `Low(a) = 0` and `High(a) = ` the actual's own element count minus one,
  **regardless of what bounds the actual was itself declared with** —
  `CodeGenProcs.cpp`'s prologue normalizes whatever bounds the caller
  passes on entry. Two names sharing one parameter group (`a, b: array of
  Integer`) size **independently** — confirmed against `fpc -Mtp` — and
  ordinary (non-`var`) open-array parameters copy on entry while a `var`
  open-array parameter aliases the actual, the same value-vs-`var` split
  every other parameter kind already has.

`SizeOf`/`High`/`Low` (below) and the string routines above compose with
all three of these forms freely — an open array of `Word`, or a `const`
record parameter with a `ShortString` field, work exactly as their
individual documentation implies with no special interaction of its own.

## New builtins

**`SizeOf`/`High`/`Low`** are, syntactically, the one argument *shape* new
to this compiler: the sole argument may be a **type name**
(`SizeOf(Integer)`) rather than a value expression, admitted only in this
one position (`Parser::parseSizeHighLowArg`), alongside still accepting an
ordinary value expression exactly like real FPC (`SizeOf(x)`, `High(arr)`).
Neither ever evaluates its argument as an expression — a type name has no
value to evaluate, and a value argument is only ever asked for its *static
type* (`SizeOf(arr[F])` does not call `F`), the same unevaluated-operand
rule C's own `sizeof` follows. `SizeOf`/`High`/`Low` all fold in constant
expressions too (`const BufSize = 4 * SizeOf(Integer)`). `High`/`Low`
answer *in the argument's own type* — `High(Byte)` is a `Byte`, not a bare
`Integer` — the same "stays in the argument's own type" rule `succ`/`pred`
already follow. For a Turbo open-array parameter specifically, `Low`/`High`
read this activation's own synthesized runtime bound slots rather than any
static range, since an open array's extent is a run-time fact of the actual
passed, not a compile-time one.

**`Hi`/`Lo`/`Swap`** are **FPC's size-aware versions — a deliberate,
permanent divergence from literal Turbo Pascal 7**, whose `Hi`/`Lo`/`Swap`
only ever worked on a 16-bit value. `fpc -Mtp` instead sizes all three off
the argument's own width, and plang follows `fpc -Mtp` here for the same
"targets FPC `-Mtp` semantics" reason the sized-integer ladder's naming
does: `Hi`/`Lo` answer in an **unsigned integer half the argument's own
width** (`Hi`/`Lo(Word) -> Byte`, `Hi`/`Lo(LongInt) -> Word`,
`Hi`/`Lo(Int64) -> LongWord`), and `Swap` rotates by half the width (a byte
swap at 16 bits, a word swap at 32, a doubleword swap at 64) — one formula
covers every width the ladder provides. **All three require a real integer
argument at least 16 bits wide**: `ShortInt`/`Byte` (8 bits) have no
separate high and low half to name at all, and are rejected
(`err_hi_lo_swap_argument`), not merely narrowed to something degenerate.
Verified bit-for-bit against `fpc -Mtp` 3.2.2 on `Word` and `LongInt`
values.

**`Include(s, x)`/`Exclude(s, x)`** are `s := s + [x]`/`s := s - [x]` by
another name, reusing the existing set-literal/union/difference codegen
primitives directly rather than a new mechanism.

**`Inc(x[, n])`/`Dec(x[, n])`** mutate an ordinal variable in place by `n`
(defaulting to `1`) — for a `PChar`-like typed pointer, this advances or
retreats it the same way `p + n`/`p - n` already do. Mirrors the existing
`succ`/`pred` lowering (widen, add/subtract, range-check, narrow) but
stores back into the argument instead of returning a new value.

**`FillChar(var X; Count: Integer; Value)`/`Move(const Source; var Dest;
Count: Integer)`** — `X`/`Source`/`Dest` are "untyped" the same way real
Turbo Pascal's are: any variable, addressed directly, its own declared type
not otherwise examined. `Count` is a **byte** count in both, matching FPC
field practice (not an element count).

**`GetMem(var P: Pointer; Size: Int64)`/`FreeMem(P: Pointer[, Size:
Int64])`** are a wholly separate, **non-aborting** allocation entry point
pair from `New`/`Dispose`, which keep aborting the process unconditionally
on out-of-memory. `Size` is `Int64`, not real Borland TP7's 16-bit `Word`:
`fpc -Mtp`'s own `GetMem` already widened this well past 65535, and
`HeapError`'s own `Size` parameter (below) matches for the same reason.
With no `HeapError` installed, a failing `GetMem` returns `nil` rather than
halting with Runtime error 203 the way real Borland's actual default does —
a **deliberate, documented divergence** (no local `fpc` build implements
`HeapError` at all to check against: modern FPC replaced it outright with a
`TMemoryManager`/`SetMemoryManager` architecture), chosen so a plang Turbo
program can check `GetMem`'s result for `nil` without installing a handler
at all, which is this pair's whole reason for existing separately from
`New`/`Dispose` in the first place.

**`HeapError: function(Size: Int64): Int64`** is a settable **procedural
value** (reusing the procedural types/values machinery just above, not a
new function-pointer mechanism) `GetMem` calls through on an allocation
failure. Returning `1` makes `GetMem` return `nil` (matches real Borland);
returning anything else reports Runtime error 203, the same way an actual
numbered range/overflow check does. `Int64`, not Borland's `Word`/`Integer`:
the runtime calls through this as a raw C function pointer directly, not
through ordinary generated-IR procedural-value call machinery, and only a
full 64-bit return is free of the x86-64 SysV ABI's unspecified-upper-bits
hazard a narrower one would risk.

**`ExitProc: procedure`**, another settable procedural value, is hooked
into the already-working `plang_module_finals_run`/`Halt` chain (the same
one issue #242 fixed for a module's `to end do`), so a program's assigned
handler runs on `Halt`, on `RunError`, and on normal program termination
alike — with no separate "and also run `ExitProc`" step needed at any of
the three.

**`ErrorAddr: Pointer`** is **deliberately simplified**: it is set only at
the two places a plang Turbo program's own control flow reports a genuine
fault — `RunError` (including every numbered range/overflow/... check,
which routes through the same reporter) and `Halt` for a **nonzero**
status only (`Halt(0)` is an ordinary successful exit, not an error) — not
wired to every individual runtime-error call site. Set *before* the
`ExitProc` chain above runs, so a custom `ExitProc` — real Turbo Pascal
field practice's most common reason to read `ErrorAddr` at all — sees the
right value from inside its own call.

**`ParamCount`/`ParamStr(n)`** read back the real `argc`/`argv` every
compiled program's C `main` now receives — `int main(int argc, char**
argv)`, unconditionally, for every dialect, not just Turbo, since ISO 7185
and Extended Pascal programs compile through the identical `emitMain` --
via a new `plang_set_args`, called as `main`'s first instruction. `ParamStr` returns
a capacity-255 `ShortString`; `n` outside `0..ParamCount` answers `''`
rather than an error, matching `fpc -Mtp`. `ParamCount`, like `eof`/`eoln`,
may be written bare, with no parentheses at all.

---

# Tier 3: the System-unit file runtime, and `Random`

Tier 1 is syntax and directives; Tier 2 is the type system and its string
routines. Tier 3 is the part of the System unit a real Turbo program
actually does file I/O with: a genuine binary/text file model distinct
from ISO 7185/Extended Pascal's (`Assign`/`Reset`/`Rewrite`/`Append`/
`Close`), the `InOutRes`/`IOResult`/`{$I+}`/`{$I-}` graceful-degradation
contract that lets a program recover from a real I/O failure instead of
aborting, and the untyped-file record-oriented routines
(`BlockRead`/`BlockWrite`/`Seek`/...) real TP programs use for random
access. Every feature below is gated to `-std=turbo` and rejected under
`-std=iso7185`/`-std=iso10206`, exactly like Tier 1/2's own extensions —
see each feature's own `*-is-turbo-only-not-available-under-iso7185-or-
extended-pascal.pas` sibling under `test/Driver/Turbo/` for the rejection
itself. `GetMem`/`FreeMem`/`HeapError`/`ExitProc`/`ErrorAddr`/
`ParamCount`/`ParamStr` — chronologically also part of this tier's own
program-control work — are already documented above, in Tier 2's "New
builtins" section, rather than repeated here; `RunError` and Turbo's other
numbered run-time errors are documented in Tier 1's "Run-time error codes"
section.

## The file model: `Assign`/`Reset`/`Rewrite`/`Append`/`Close`, `FileMode`

Real Turbo Pascal's own file-opening idiom is a two-step **bind, then
open**, unlike ISO 7185/Extended Pascal's one-step `reset(f, 'name')`/
`rewrite(f, 'name')` (`docs/conformance.md`): `Assign(f, name)` records a
name against a file variable without touching the filesystem at all, and a
later `Reset(f)`/`Rewrite(f)`/`Append(f)` — no filename argument — opens
whatever name the most recent `Assign` (or, for `Rewrite`/`Append` after a
`Rename`, the file's own possibly-updated bound name) left behind. This
retires the plain two-argument `Reset(f, 'name')`/`Rewrite(f, 'name')`
implicit-assign form plang used to accept as a convenience: real Turbo
Pascal's own second argument there is an **integer** `RecSize` for an
untyped file (see below), not a filename, and `fpc -Mtp` rejects a string
there with an incompatible-type error — plang now matches, with a real
Sema diagnostic rather than silently accepting the old shape (see
`reset-rewrite-two-argument-implicit-assign-was-retired-explicit-assign-
still-binds-the-name.pas`, `test/CodeGen/Turbo/`).

`Assign(f, '')` — an **empty name** — is a real, documented TP idiom for
binding a file to the console instead of a real path: a following
`Reset(f)` attaches `f` to `stdin`, a following `Rewrite(f)`/`Append(f)`
attaches it to `stdout`, both literally the same C `stdin`/`stdout`
streams a bare `read`/`readln`/`write`/`writeln` (no file argument) already
reaches — so a program can freely mix `readln(f, s)` against a
console-bound `f` with a bare `readln(s)` and see the identical input
stream. `Assign(Output, name)` followed by `Rewrite(Output)` — the
**predefined** `Output` variable itself, not a program-declared file —
redirects every subsequent bare `write`/`writeln` to that file;
`Assign(Output, '')` + `Rewrite(Output)` un-redirects it back to the
console. See "Input/Output as real variables" below for why `Output` can
be `Assign`ed at all.

`Close(f)` closes the underlying stream but, unlike ISO/EP's `close`,
performs none of the ISO/EP path's extra bookkeeping (no
unterminated-line finishing beyond what the file's own dialect-appropriate
write path already does — see "`file of char`" below — and no lingering
component-buffer free) and **does not un-bind the name**: a following
`Reset`/`Rewrite`/`Append` with no intervening `Assign` reopens exactly
the name that was already bound, confirmed against `fpc -Mtp`.

`FileMode: Integer` (Sema's second predefined mutable `Var`, after
`ExitCode`, registered the identical way) defaults to `2` ("read-write"),
matching real Borland/FPC — see `filemode-defaults-to-2-and-is-
assignable.pas`. `Reset` now honors it: `0` opens read-only, `1`
write-only (a raw `open(O_WRONLY)`, since `Reset` must never truncate and
no `fopen(3)` mode string opens write-only without also creating or
truncating), and `2` (the default) read-write — confirmed against
`fpc -Mtp`. A direction violation against a named file (e.g. `Write`
after a `FileMode`-`0` `Reset`) sets `InOutRes` (`105`/`104`, matching
Borland's own "file not open for output/input" codes) under `{$I-}`
rather than aborting, the same graceful-degradation contract every other
Turbo I/O failure gets.

## `InOutRes` and `IOResult`

`InOutRes: Integer`, a hidden global (not itself a predefined identifier a
program can name — real Turbo Pascal does not expose it directly either),
latches the numbered result of the most recent I/O operation. `IOResult`
(callable as `IOResult()` or bare `IOResult`, both syntactic forms reaching
the identical runtime accessor) **reads InOutRes and clears it to 0 in the
same call** — a second, immediately following `IOResult` read always
answers `0`, even with nothing else run in between (`ioresult-reads-and-
clears-inoutres.pas`).

**A pending, unread `InOutRes` is not overwritten by a later failing
operation** — confirmed against `fpc -Mtp`, and the least obvious part of
this contract: once one operation under `{$I-}` leaves `InOutRes` pending,
a SECOND, independently-failing operation does not replace that code with
its own; only an explicit `IOResult` read clears the latch, ready to
capture the next failure fresh (`a-pending-inoutres-is-not-overwritten-by-
a-later-failing-operation.pas`, and, driven by a full realistic
reopen-across-two-different-failures scenario rather than one call,
`test/Turbo/pending-ioresult-survives-a-reopen-and-a-later-operation.pas`).

Every failure this tier's file-model routines can raise reaches `InOutRes`
through one shared helper (`setInOutResIfClear`, `runtime/plang_file.cpp`)
and one shared errno-to-code table (`plang_tp_posix_to_run_error`) for
every failure that comes from a real, mapped POSIX `errno`:

| Code | Meaning | `errno` |
|---|---|---|
| 2 | File not found | `ENOENT` |
| 3 | Path/name too long | `ENAMETOOLONG` |
| 4 | Too many open files | `ENFILE`, `EMFILE` |
| 5 | File access denied | `EACCES`, `EROFS`, `EEXIST`, `ENOTEMPTY`, `EBUSY`, `ENOTDIR`, `EISDIR` |
| 100 | Disk read error | `EPIPE`, `EINTR`, `EIO`, `EAGAIN`, `ENOSPC` (via a real open failure), or a short `BlockRead` with no `Result` argument |
| 101 | Disk write error | Same `errno` set as 100, or a short `BlockWrite` with no `Result` argument |
| 102 | File not assigned | `Erase`/`Rename` against a file that is still open, or was never `Assign`ed at all |
| 103 | File not open | Any operation against a file variable that was never successfully `Reset`/`Rewrite`/`Append`ed |
| 218 | Invalid numeric format passed to the OS (`EINVAL`) | `Seek` with a negative record number |

Every table entry above is reachable from a genuine filesystem condition —
not simulated — and is exercised end to end, both individually
(`test/CodeGen/Turbo/`) and as one combined program driving all of them
back-to-back
(`test/Turbo/ioresult-matrix-real-filesystem-driven-error-codes.pas`).
Two gaps are worth calling out explicitly: code 3 (`ENAMETOOLONG`) is not
practically reachable from a plang Turbo program at all, since `Assign`'s
name parameter is a capacity-255 `ShortString` and every filesystem this
project targets has a 255-byte `NAME_MAX` — no string a Turbo program can
even construct exceeds it. Code 4 (`EMFILE`/`ENFILE`) needs a real
per-process file-descriptor limit lower than the test process's own
default, which this project's test suite has no portable, CI-safe way to
arrange (no `ulimit` in lit's own restricted RUN-line shell — see
`test/lit.cfg.py`'s comment on `%checkexit`/`%hold_stdin_open` for the
general shape of what that shell cannot do) — both codes are real and
reachable in principle, just not exercised by this project's own test
suite.

**106** ("Invalid numeric format") is deliberately absent from the table
above: real Turbo Pascal treats a malformed `read`/`readln` numeric token
as gated by `{$I-}` too (confirmed against `fpc -Mtp`), and plang's own
`read`/`readln` now honors that as well — under `{$I-}`, `InOutRes` reads
`106` and the destination variable is set to `0`; under the default
`{$I+}` it aborts with `Runtime error 106`, matching every other
`InOutRes` code's own default-abort behavior — see
`read-of-malformed-numeric-input-honors-i-minus.pas`
(`test/Turbo/`).

## `{$I+}`/`{$I-}`: automatic checks

Turbo's `IOChecks` switch (`{$I+}`/`{$I-}`, `include/plang/Basic/
CompilerSwitches.def`) is **textual and positional**, like every other
switch this project models: a checked statement's own automatic abort is
decided by that STATEMENT's source position, never by which call in a
sequence actually produced the pending failure. Under the real Turbo
default (`{$I+}`, unless a program says otherwise), a checked I/O
statement — `Reset`/`Rewrite`/`Append`/`Close`/`Read`/`Readln`/`Write`/
`Writeln`/`BlockRead`/`BlockWrite`/`Seek`/`Truncate`/`Erase`/`Rename`/
`Flush`, and a bare bodyless `Read`/`Write` (no file argument) reading
`Input`/writing `Output` too — emits a `plang_iocheck()` call right after
itself (`RangeCheckGuards::ioChecksAt`, `CGProcCall::emitIoCheckIfNeeded`),
which reports and aborts with the pending `InOutRes` code as the exit
status (`Runtime error <n> at $<addr>`, never the shared ISO/EP
`plang_err_*` wording or exit status 70) if one is still latched. Under
`{$I-}`, that call is not emitted for a checked statement at that
position, so a failure there leaves `InOutRes` pending and lets the
program keep running.

Which position's checkpoint actually reports a given failure is the
subtle half of this contract: a `Reset` failing under `{$I-}`, followed by
a `Read` that then runs under `{$I+}`, aborts at the `Read`'s own
checkpoint — but with `Reset`'s ORIGINAL code, not whatever the `Read`
would independently have produced (`io-plus-aborts-at-the-next-checked-
operation-not-the-failing-one.pas`) — the same "first pending code
survives" rule `InOutRes`'s own read-and-clear contract follows above.
Both the `{$I-}` (`test/Turbo/ioresult-matrix-real-filesystem-driven-
error-codes.pas`) and default `{$I+}` (`test/Turbo/default-i-plus-exit-
status-matches-the-ioresult-code.pas`) sides of this contract are
exercised end to end for the same underlying failures, confirming the
exit status and `InOutRes` code agree.

## `RecSize`

An untyped file's record size for `BlockRead`/`BlockWrite`/`Seek`/
`FilePos`/`FileSize` purposes is either an explicit integer second
argument to `Reset`/`Rewrite` (`Reset(f, 4)`), or, when none is given,
Turbo's own documented default of **128**. A typed file's `RecSize` is
always `SizeOf` its element type, computed by codegen, never explicit. A
`RecSize` of exactly `0` is real Borland/FPC field practice's own special
case: it sets `InOutRes` to `2` ("file not found") without attempting to
open anything at all, rather than a division-by-zero or a silently-wrong
`FileSize`.

`FileSize` **floors** when a file's real byte length is not an exact
multiple of its own `RecSize` — confirmed against `fpc -Mtp`: a 5-byte
file reopened with `RecSize` 2 reports `FileSize` 2, not 3 (rounded up).
The two pieces compose concretely: the SAME 16-byte file reports
`FileSize` 4 reopened with an explicit `RecSize` of 4 (`16 div 4`, exact),
but `FileSize` 0 reopened with no explicit `RecSize` at all — the
untyped-file default of 128 — since the file is shorter than even one
128-byte record (`16 div 128`, floored to 0, not rounded up to 1); see
`test/Turbo/reset-recsize-interacts-with-filesizes-floor-division.pas`.

## `BlockRead`/`BlockWrite`/`Seek`/`FilePos`/`FileSize`/`Truncate`/`Erase`/`Rename`/`Flush`/`SetTextBuf`/`SeekEof`/`SeekEoln`

`BlockRead(f, buf, count[, result])`/`BlockWrite(f, buf, count[, result])`
transfer `count` whole `RecSize`-sized records between `buf` (an untyped,
raw pointer — any variable, its own declared type not otherwise examined)
and `f`. Their optional trailing `result` argument changes what a SHORT
transfer means: WITH it, `result` silently receives however many whole
records were actually transferred and `InOutRes` stays `0` — not an
error; WITHOUT it, a short transfer sets `InOutRes` (100 for `BlockRead`,
101 for `BlockWrite`) as a real failure. Both arities, and the realistic
reason a program picks one over the other (recover with the true count vs.
treat a short transfer as fatal), are exercised together against one
shared fixture in `test/Turbo/blockread-blockwrite-combined-with-and-
without-result.pas`.

`Seek(f, n)` positions `f` at record `n` (0-relative, `RecSize` bytes from
the start); seeking PAST the current end of file is legal and not an
error — real Turbo Pascal programs rely on this to extend a file (seek
past the end, then `Write`/`BlockWrite`) — but a NEGATIVE `n` is (`InOutRes`
218, `EINVAL`). `FilePos(f)` reads back the current record position;
`FileSize(f)` reads the file's own total record count (floored — see
"`RecSize`" above). `Truncate(f)` discards everything from the CURRENT
position to the file's previous end, leaving everything before it
untouched. All four compose in one realistic scenario — write, seek
partway in, truncate, confirm `FileSize` reflects it, seek back and
confirm the surviving records are intact — in
`test/Turbo/seek-filepos-filesize-truncate-combined-scenario.pas`.

`Erase(f)`/`Rename(f, newname)` act on `f`'s own bound name and require
`f` be closed first — calling either against a still-open `f` (or one
never `Assign`ed at all) sets `InOutRes` 102 and performs nothing. A
successful `Rename` updates `f`'s own bound name, so a following
`Reset`/`Rewrite`/`Append` with no intervening `Assign` reaches the
RENAMED file, matching `fpc -Mtp`.

`Flush(f)` flushes `f`'s buffered output without closing it; always
`InOutRes` 0 on a valid, open file.

`SetTextBuf(f, buf, size)` overrides `f`'s own I/O buffering with
caller-supplied storage. This is a **deliberate, documented deviation**
from real Turbo Pascal's exact contract: plang's file model is a thin
wrapper over C stdio, with no "pending buffer, not yet attached to a
stream" slot the way Borland's own `TextRec` has, so `SetTextBuf` called
BEFORE `f` is opened is a silent no-op (real Turbo Pascal's own idiom, but
inert here), while called AFTER `Reset`/`Rewrite`/`Append` it takes effect
immediately via `setvbuf(3)` — the reverse of real Turbo Pascal's own
ordering.

`SeekEof(f)`/`SeekEoln(f)` are the CONSUMING counterparts of `Eof`/`Eoln`:
`SeekEof` skips past any blanks, tabs, and line markers ahead of the
current position before testing; `SeekEoln` skips only blanks and tabs (a
line marker is itself what `Eoln` tests for, so `SeekEoln` does not cross
one).

## `text` vs. `file of char`

Real Turbo Pascal gives `text` its own distinct predefined type — "the
standard type Text ... is not the same as File Of Char," Borland's own
manual — and treats `file of char` as an ordinary **binary** file: each
`Char` component is one raw byte, with no line-ending/formatting
convention applied on `Close`. This is a genuine dialect REVERSAL from ISO
7185 §6.4.3.5 and Extended Pascal, where `file of char` has no separate
identity from `text` at all ("a file of the type char is termed a
textfile") — writing two characters and closing produces exactly 2 raw
bytes under `-std=turbo`, but 3 (`"AB\n"`, the trailing newline the text
path appends to finish an unterminated line) under `-std=iso7185`/
`-std=iso10206`, confirmed side by side against the identical two
`write(f, ch)` calls in
`test/Turbo/file-of-char-binary-under-turbo-text-under-iso7185.pas` — the
two dialects' own real file-opening idioms differ too (`Assign`+`Rewrite`
under Turbo vs. `Rewrite(f, 'name')` under ISO 7185, since the two-argument
`Rewrite` name-binding form was retired under Turbo — see "The file model"
above), so that file is two small `split-file` variants rather than one
byte-identical source.

## Input/Output as real variables

`Input`/`Output` are ordinary, `Assign`-able `Text` variables under
`-std=turbo` — not fixed, unredirectable handles the way a bare
`read`/`write` might suggest. `Assign(f, '')` binds a file to the console
(see "The file model" above); applying that same idiom to the PREDEFINED
`Output` (`Assign(Output, name)`, `Rewrite(Output)`) redirects every
following bare `Writeln` to a named file, and `Assign(Output, '')` +
`Rewrite(Output)` sends it back to the console — a full redirect/
un-redirect lifecycle, bare console output before, a real file during, and
back to the console after, all exercised in one program in
`test/Turbo/assign-output-redirect-and-unredirect-full-lifecycle.pas`.
`Input`'s own analogous redirection makes a bare `read`/`readln`/`eof`/
`eoln` (no file argument) follow whatever `Input` is currently bound to
(`bare-readln-with-no-file-argument-reads-from-a-redirected-input.pas`,
`bare-eof-and-eoln-follow-a-redirected-input.pas`, `test/CodeGen/Turbo/`).

## `Random`, `Randomize`, `RandSeed`, `Int`, `Frac`

`Random` shares one name across two call shapes: `Random` (no argument)
answers a `Real` in `[0, 1)`; `Random(Range)` advances the SAME generator
state and answers an ordinal value in `[0, Range)`, **in `Range`'s own
type** — the same "stays in the argument's own type" rule `Abs`/`Sqr`/
`Succ`/`Pred`/`High`/`Low` already follow. `RandSeed: Integer` (a
predefined mutable `Var`, registered the same way `ExitCode`/`FileMode`
are) is the generator's own visible state: setting it to the same value
before two separate runs makes `Random`'s sequence deterministic and
reproducible across those runs
(`randseed-set-to-the-same-value-makes-random-deterministic.pas`).
`Randomize` reseeds `RandSeed` from a real source of entropy (wall-clock
time), so successive RUNS of the same program get a different sequence
(`randomize-reseeds-randseed-differently-across-separate-runs.pas`).

This is plang's OWN hand-rolled generator (`runtime/plang_math.cpp`) —
**no claim is made that its sequence matches** real Borland Turbo Pascal
7's own 32-bit LCG or Free Pascal's Mersenne Twister; the two do not match
each other either, and bit-for-bit compatibility with either was never a
goal. A program that only needs `Random`'s documented RANGE and
`RandSeed`'s documented determinism-when-seeded behavior — not a specific
sequence of values — is unaffected.

`Int(x)`/`Frac(x)` split `x` into its integer part (toward zero, like
`Trunc`, but answering a `Real` rather than an ordinal — `Int(1e30)` is
simply `1e30`, not the runtime error `Trunc(1e30)` correctly raises) and
its fractional part (`x - Int(x)`, keeping `x`'s own sign: `Frac(-3.7)` is
`-0.7`, not `0.3` — there is no `floor` here).

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

### Permanent: no `QWord` literal syntax — build one with a typecast

Neither Turbo Pascal nor plang has an *unsigned* integer-literal syntax, and
plang's own integer literals are `int64_t` end to end, from the scanner
through constant folding — so a literal past `Int64`'s own maximum
(`9223372036854775807`) is rejected by the lexer's range check regardless of
the destination type, before Sema ever sees which type it's headed for. A
`QWord` value in that upper half of its range (`9223372036854775808` through
`18446744073709551615`) therefore cannot be written as a literal at all —
build it with a typecast or arithmetic a `QWord` variable can hold instead,
exactly the two idioms real `fpc -Mtp` source uses for the identical reason:
`QWord(-1)` for the all-ones maximum, or `QWord(9223372036854775807) + 1`
for the value one past `Int64`'s own maximum. This is permanent — not a gap
in `QWord`'s own write/read support (which is complete and covers the full
unsigned range end to end), but a limitation of literal syntax that predates
`QWord` and affects only how its uppermost values get *into* a program's
source text.

### Permanent: a set is always 256 bits wide, in every dialect — narrowing does not reach it

The sized-integer ladder's per-declaration storage narrowing (see "Narrowed
subrange and enum storage" above) applies to integers, subranges, and
enumerations — it does **not** apply to `set of T`. Every set, under every
dialect including Turbo, is lowered to a fixed 256-bit (32-byte) bitmask
(`PlangMaxSetElements`, `Sema/Type.h`) regardless of how few elements its
base type actually has: `set of 0..9` occupies the same 32 bytes `set of
char` does. This is a pre-existing, dialect-independent design choice (the
representation that lets any base type up to 256 elements share one set
implementation), not something Tier 2's narrowing work was ever scoped to
touch, and not a bug — a real `fpc -Mtp` set is itself sized per-declaration
in a way plang's is not, so this is a genuine, permanent representational
difference worth stating plainly rather than leaving a reader to assume
narrowing reaches everywhere it plausibly could.

### Permanent: `SetLength` is clamped/safe where real `fpc -Mtp` is not

`SetLength(var s, newLength)` sets `s`'s own length byte directly, clamped
to `[0, s`'s declared capacity`]`. Real `fpc -Mtp` has two confirmed quirks
here plang deliberately does not reproduce, both memory-safety or
input-validation holes rather than "ambiguous field practice" worth
matching: it lets `SetLength` write a length byte **past** a narrow
`string[N]`'s own physical `(N+1)`-byte storage with no clamp at all — a
genuine buffer overrun (`SetLength(s5, 100)` on a `string[5]` left
`Length(s5) = 100` and a later `s5[6]` read/write went out of bounds,
`Runtime error 201` under `-Cr`) — and it writes a **negative** `newLength`
through as a raw byte reinterpretation rather than refusing or clamping it
(`SetLength(s, -1)` left `Length(s) = 255`, i.e. real `fpc`'s implementation
is simply an unchecked `PByte(@s)^ := Byte(newLength)`). plang clamps to
`[0, cap]` in both directions instead, the same way every other
`plang_sstr_*` function already does — a deliberate safety improvement, not
an oversight, and permanent: there is no reason to reproduce a real buffer
overrun for compatibility's own sake.

### `Hi`/`Lo`/`Swap` and `Val`'s two scanner quirks: already covered above

Two more permanent, deliberate divergences from real Turbo Pascal 7 /
`fpc -Mtp` are documented in full where they're most useful to a reader —
alongside the feature itself, in the Tier 2 sections above — rather than
repeated here: `Hi`/`Lo`/`Swap` being FPC's size-aware versions rather than
literal TP7's 16-bit-only ones (see "New builtins"), and `Val`'s two
confirmed, unreproduced `fpc -Mtp` inconsistencies around a trailing
`e`/`e+`/`e-` with no exponent digits (see "`Val`'s error contract").

### Fixed gap (was a known gap): `read`/`readln` of a malformed numeric token now honors `{$I-}`

Real Turbo Pascal/`fpc -Mtp` treats a malformed numeric token read via
`read`/`readln` (Runtime error 106, "Invalid numeric format") as an
ORDINARY I/O failure subject to `{$I-}`/`{$I+}` — confirmed directly: under
`{$I-}`, `fpc -Mtp` sets `IOResult` to 106, assigns the destination
variable 0, and keeps running. plang's own `plang_read_file_i64_turbo`/
`plang_read_file_u64_turbo`/`plang_read_file_f64_turbo`
(`runtime/plang_file.cpp` — the entry points a `-std=turbo`
`read`/`readln` actually reaches, including a bare `read` with no
explicit file variable, since Turbo's own `input` is modeled as a real
`PascalFile` binding) used to call the unconditional, `[[noreturn]]`
`plang_tp_runerror(106)` directly on a malformed token, aborting the
process INSIDE the read call before `CGProcCall.cpp`'s
`emitIoCheckIfNeeded` machinery — already wired in after every
`read`/`readln` and already correctly honoring `{$I-}`/`{$I+}` for every
OTHER Turbo I/O failure — ever got a chance to run. Fixed by setting the
destination variable to 0 and `InOutRes` to 106 (the same
`setInOutResIfClear` "a pending, unread error is not overwritten"
contract every other Turbo I/O failure in `plang_file.cpp` already uses)
and returning normally, letting the existing checked-statement machinery
decide whether to abort — exactly like every other Turbo I/O failure.
`plang_io.cpp`'s `plang_read_i64_turbo`/`_u64_turbo`/`_f64_turbo` (the
stdin-only siblings, never actually reached under `-std=turbo` since a
bare `read` always has a `PascalFile`) got the identical fix for
consistency even though they are currently dead code on that path. See
`test/Turbo/read-of-malformed-numeric-input-honors-i-minus.pas`.

### Fixed gap (was a known gap): `Reset` now opens read-write, honoring `FileMode`'s own documented default

`FileMode` defaults to 2 ("read-write" — see "The file model" above), and
real Turbo Pascal/`fpc -Mtp` honors that concretely: `Reset` opens the
underlying file read-write, so a `Write` (or a `Seek`+`Truncate`) against a
file the program only ever `Reset` — never `Rewrite`/`Append` — works,
the "load a record, seek back, patch it in place" idiom real TP field
practice depends on. plang's own `plang_tp_reset` (`runtime/
plang_file.cpp`) now chooses its open mode from the CURRENT value of
`FileMode`, confirmed against `fpc -Mtp`: `0` opens read-only (`fopen`
`"r"`, unchanged from before), `2` (the default) opens read-write
(`"r+"`), and `1` (write-only) opens via a raw `open(O_WRONLY)` — no
`fopen(3)` mode string opens write-only without also creating or
truncating, and `Reset` must never truncate, unlike `Rewrite`. A genuine
direction violation against a named file (e.g. `Write` after a
`FileMode`-`0` `Reset`) now routes through a Turbo-only, non-aborting
twin of the wrong-mode/wrong-direction checks
(`tpTrapOnStreamError`/`tpTrapOnWrongDirection`, following the
`tpFileReady` naming convention this tier already established) that sets
`InOutRes` to `105`/`104` ("file not open for output"/"input", Borland's
own documented codes, confirmed against `fpc -Mtp`) under `{$I-}` instead
of aborting through the SHARED, ISO/EP-reachable
`plang_err_file_wrong_mode` — which is completely unchanged and still
aborts unconditionally on a mode violation, correct behavior for those
dialects. See `test/Turbo/reset-opens-read-write.pas` and
`test/Turbo/seek-truncate-after-fresh-reset.pas` (the Reset-session
Seek+Truncate path this fix newly enables — see
`test/Turbo/seek-filepos-filesize-truncate-combined-scenario.pas`'s own
comment for the Rewrite-session workaround this test's sibling no longer
needs).

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

# Tier 4: units — `uses`, separate compilation, and the shipped RTL

Tiers 1-3 are all one compilation unit's own worth of dialect: syntax,
directives, the type system, the System-unit file/heap/program-control
runtime. Tier 4 is what a real multi-file Turbo program is built out of —
`unit`/`interface`/`implementation`/`uses`, TP's own scoping rules, real
separate compilation (a unit compiled once, used by many programs that
never see its source again), and the four real, shipped standard-library
units a Turbo program actually `uses`: `Crt`, `Dos`, `Printer`, `Strings`.
Every feature below is gated to `-std=turbo` the same way Tiers 1-3's own
extensions are — a `unit` file, or a `uses` clause naming anything beyond
the always-implicit `System`, is rejected under `-std=iso7185`/
`-std=iso10206`.

This tier's own integration-level test corpus lives under
`test/Turbo/Units/` (richer, multi-unit, multi-file scenarios — a small
number of realistic programs, not one lit test per isolated behavior, the
same split `test/Turbo/`'s own Tier 3 capstone already established); the
per-PR unit-level coverage each item below cites lives under
`test/Parse/ParserTurboUnits/`, `test/Sema/SemaTurboUnitScoping/`, and
`test/Driver/Turbo/`.

## `unit`/`interface`/`implementation`/`uses` syntax

A unit source file's top-level shape is fixed, in this order: `unit
<Name>;`, an `interface` section (its own optional `uses` clause, then
`const`/`type`/`var` declarations and procedure/function **headings** —
no bodies), an `implementation` section (its own optional, independent
`uses` clause, then the bodies for everything the interface declared,
plus any private `const`/`type`/`var`/procedure/function of its own that
the interface never mentions), and a final `end.` — no `begin` is
required before it (see "Unit initialization sections do not run
automatically" below for what an optional `begin...end` there does and
does not do). A **program** file's own `uses` clause, if any, comes
immediately after the `program` heading, before any declaration.

`uses UnitA, UnitB, UnitC;` — a comma-separated list, order significant
(see "Scoping" below). The interface's own `uses` and the implementation's
own `uses` are two syntactically and semantically **independent** clauses
(`interface-uses-and-implementation-uses-are-independent-clauses.pas`):
a unit named in one is not automatically visible through the other, and
each is where "who does this unit's own type-checking depend on" is
actually decided for that section — see "Mutual `uses`" below for exactly
what that independence buys.

## Scoping: last-used-unit-wins, qualification, and implicit `System`

Every `uses`d unit pushes its own symbol-table scope, one per unit, in
`uses`-clause order (`Sema::pushUnitUsesScopes`) — **not** one shared scope
all `uses`d units' exports get merged into. An ordinary, unqualified
lookup (`SymbolTable::lookup`'s already-existing innermost-first search)
therefore resolves a name two different units both export to whichever
one was named **last**: real Turbo Pascal's own well-known "last unit
wins" rule, confirmed against real `fpc -Mtp` printing the identical
answer for the identical two-unit setup. `UnitName.Identifier` — explicit
qualification — reaches a **specific** unit's export directly, including
one a bare read would not see because a later `uses`d unit shadows it;
this is the only way to reach the shadowed unit's own value at all. See
`a-later-uses-clauses-unit-shadows-an-earlier-ones-same-named-export.pas`
(`test/Driver/Turbo/`) for the minimal two-unit proof and
`last-unit-wins-shadowing-three-units-realistic-config.pas`
(`test/Turbo/Units/`) for the same rule exercised across three units with
both the bare-name and every qualified form checked in one program.

`System` — the implicit unit every Turbo program and unit gets without
writing `uses System` — is pushed first, so it sits at the **bottom** of
every lookup: any real, explicitly-`uses`d unit's own export of the same
name shadows `System`'s own, exactly like any other last-wins pair
(`a-real-units-export-shadows-the-implicit-system-unit.pas`).

**Transitively** `uses`d units' own identifiers are not visible at all — a
program that `uses UnitA`, where `UnitA`'s own interface `uses UnitB`,
sees everything `UnitA` exports but nothing `UnitB` exports unless the
program also names `UnitB` itself in its own `uses` clause (confirmed
against real `fpc -Mtp`:
`transitively-used-units-own-identifiers-are-not-exposed-real-fpc-
confirmed.pas`). A **circular interface `uses`** (`UnitA`'s interface
`uses UnitB`, `UnitB`'s interface `uses UnitA`) is a diagnosed Sema error,
not silently tolerated or infinitely recursed into
(`circular-interface-uses-is-diagnosed-not-an-infinite-recursion.pas`).

### Mutual `uses` through `implementation` — allowed

Two units' own **implementation** sections may `uses` each other — `UnitA`'s
implementation `uses UnitB`, `UnitB`'s implementation `uses UnitA` — even
though the identical shape in both units' **interface** sections is the
diagnosed circularity above. Real Turbo Pascal allows this too (confirmed
against `fpc -Mtp`), and it is not actually a circular *dependency* in what
must be known to type-check: by the time either unit's implementation needs
the other, it only ever needs the other's already-fully-resolved
INTERFACE, never the other's implementation. See
`two-units-mutual-implementation-uses-compiles-and-links-real-fpc-
confirmed.pas` (`test/Driver/Turbo/`) for the minimal two-procedure proof
and `mutual-implementation-uses-two-units-real-work.pas`
(`test/Turbo/Units/`) for the same shape with both directions doing real,
data-dependent work — one unit validating input and logging into the
other, the other formatting output by calling back into the first —
rather than each side just proving the link graph resolves.

## Real separate compilation: `.tui` files, `-c`, and the shipped-RTL search path

`plang -std=turbo -c unit.pas -o unit.o` compiles a unit **on its own**,
producing both the real object file (`unit.o` — empty if the unit exports
nothing but constants/types, since those need no code) and a `.tui`
("turbo unit interface") file next to it — a small, re-parseable Pascal
source fragment holding exactly the unit's own interface section, written
by the identical `typeDenoterToString`/`exprToString` machinery the `.pmi`
writer already used for Extended Pascal modules (`docs/modules.md`). A
later `plang ... program.pas unit.o -o program` that `uses` that unit reads
the `.tui`, not the original `.pas`, to type-check against it — proven the
strongest available way in this project's own test suite by deleting the
unit's `.pas` source entirely between the two compiles and confirming the
importer's compile and the final program's runtime behavior are both
unaffected (`a-unit-compiled-standalone-can-be-used-by-a-program-that-
never-sees-its-source.pas`, and, at three-unit scale,
`three-units-separate-compilation-real-string-pipeline.pas` in
`test/Turbo/Units/`).

The `.tui` writer covers every kind of interface declaration this tier's
own codegen wires cross-object linkage for: scalar and structured
(record/array, including nested) typed constants, sized-integer types
(`Byte`/`Word`/`ShortInt`/`LongInt`), `string[N]`, `PChar`, and procedural
types — see `a-units-sized-integer-shortstring-pchar-and-procedural-vars-
round-trip-through-the-tui.pas` and `a-units-record-and-array-typed-
constant-round-trips-through-the-tui.pas` (`test/Driver/Turbo/`) for the
single-unit proof and `word-and-record-typed-const-integration.pas`
(`test/Turbo/Units/`) for a `Word`-typed and a record-typed exported
constant consumed correctly alongside an unrelated second separately-
compiled unit, the same round-trip proven at multi-unit scale rather than
in isolation.

A `uses` clause is resolved (both for the `.tui`/`.pas` interface and, if
the unit exports anything needing real linked code, for the object file
itself) against `unitSearchPaths()`'s three tiers, in order
(`lib/Basic/UnitSearchPath.cpp`): the `PLANG_UNIT_DIR` environment
variable, `<exeDir>/../lib/plang/units` (where a real install's
`share/plang/units/` lands — see `install(DIRECTORY share/plang/units/
...)`, `CMakeLists.txt`), and a compiled-in build-tree fallback
(`${CMAKE_BINARY_DIR}/lib/plang/units`, so `ninja check-lit`'s own
in-tree `build/bin/plang` resolves `uses Crt;` with no flags at all, the
same "no flags" experience an installed `plang` gives). An explicit `-I`
on the command line, and the current directory, are checked **first**,
ahead of all three tiers — a user's own separately-compiled unit sitting
next to the program (its `.o`/`.tui`, no `.pas` required) auto-links the
identical way a shipped unit does, confirmed directly: with only
`mathunit.o` and `mathunit.tui` present (its `.pas` deleted), `plang
-std=turbo main.pas -o main` — no `-o mathunit.o` named on the command
line at all — still finds and links it; removing the `.o` too turns that
into a real `ld` "undefined symbol" failure, not a silent skip. This is
the same driver-side scan (`scanUsesClauseUnitNames`/
`findShippedUnitObject`, `lib/Driver/Driver.cpp`) that lets `uses Crt;`
compile and link with zero flags — see
`crt-uses-clause-auto-links-with-no-flags.pas` — extended here to confirm
it is not special-cased to the shipped RTL's own install location.

## The shipped units: `Crt`, `Dos`, `Printer`, `Strings`

Four real standard-library units ship with plang, installed to
`<prefix>/lib/plang/units` and auto-linked by name, the same "no flags"
experience described above — POSIX terminals only (Linux and macOS; no
Windows console), a deliberate, already-made scope call matching this
project's own CI matrix.

**`Crt`** — screen/cursor/color state, all built on ordinary `Write` and
real ANSI/VT100 escape sequences: `ClrScr`, `ClrEol`, `GotoXY(X, Y)`,
`WhereX`/`WhereY`, `TextColor`/`TextBackground` (real, readable/settable
`TextAttr: Byte`, packed exactly like real TP's — bits 0-3 foreground,
4-6 background, bit 7 blink), `Window(X1, Y1, X2, Y2)` (real,
readable/settable `WindMin`/`WindMax: Word`), and the 16 named Borland
color constants (`Black` .. `White`, plus `Blink`). `TextColor`/
`TextBackground`'s Borland-to-ANSI-SGR color mapping is not simple
arithmetic — Borland's own color order is not ANSI's — see `ApplyAttr`,
`share/plang/units/Crt.pas`, confirmed digit-for-digit against `fpc`'s own
`unix/crt.pp` `AnsiTbl`. `Delay(MS)`, `KeyPressed`, and `ReadKey` are real
OS-backed builtins (a real wait; raw, unbuffered keyboard input via
`termios`), declared in `Builtins.def` rather than as ordinary `Crt`
exports — see that file's own comment for why.

**`Dos`** — real POSIX reinterpretations of TP7's DOS-era Dos unit, every
routine backed directly by `runtime/plang_dos.cpp` (the unit's own
`implementation` bodies are never what actually runs — see that unit's
header comment): `GetDate`/`GetTime`/`SetDate`/`SetTime`, `PackTime`/
`UnpackTime` (the `DateTime` record), `Exec` (synchronous — the calling
program blocks — with the child's exit code read back through
`DosExitCode`), `DiskFree`/`DiskSize` (real `statvfs(2)`), `FindFirst`/
`FindNext`/`FindClose` (real `opendir`/`readdir`/`closedir` — the
`SearchRec` record's `Attr`/`Time`/`Size`/`Name` fields are real and
portable; its first three fields are a private, implementation-defined
replacement for real TP's opaque `Fill` bytes, carrying directory-
iteration state), `GetEnv`, and `GetDir`/`ChDir`/`MkDir`/`RmDir` (a DOS
drive-letter `Drive` parameter is accepted for signature compatibility and
always means "the current directory" — this project registers no other
drive). Every one of these reports failure through the global `DosError:
Integer` (0 on success) instead of a return value or exception — the same
shape `InOutRes`/`{$I-}` uses for file I/O, but a separate register,
confirmed against real `fpc -Mtp`.

**`Printer`** — a single exported variable, `Lst: Text`, auto-bound to a
real, writable text file the first time the program touches it — no
`Assign`/`Rewrite` of its own required, matching real TP field practice
where `Lst` just works. Because a used unit's own initialization section
does not run (see below), this binding happens through a C++ global
constructor in `runtime/plang_printer.cpp` that calls straight into the
runtime, bypassing the Pascal-level init path this tier could not rely on
— the one shipped unit that needed a workaround different from `Crt`'s own
lazy-`EnsureInit`-per-call pattern, since `Lst` is a bare variable a
program can read/assign with no intervening call to hook.

**`Strings`** — the classic null-terminated `PChar` toolbox, every routine
backed by real `runtime/plang_strings.cpp` C string primitives (`strlen`/
`strcpy`/`strcat`/`strcmp`/... under the hood): `StrLen`, `StrCopy`/
`StrLCopy`, `StrCat`/`StrLCat`, `StrComp`/`StrLComp`/`StrIComp`,
`StrPos`/`StrScan`/`StrRScan`, `StrUpper`/`StrLower`, `StrNew`/
`StrDispose` (heap-allocated `PChar`s), and `StrPCopy`/`StrPLCopy` (copy a
Pascal `string` INTO a `PChar` buffer — the `Source` parameter is `var`,
so a string literal must be assigned to a variable first, real TP/FPC
field practice for the identical reason).

`Dos` and `Strings` need no compiled object of their own to link against —
every exported routine binds directly to a `runtime/plang_*.cpp` symbol
already linked into every Turbo program, so only their `.pas`/`.tui` need
resolving. `Crt` is the one shipped unit with real Pascal-level bodies
(`ClrScr` and friends are ordinary Pascal, not `extern` declarations), so
it alone ships a real, precompiled `crt.o` (built once, at plang's own
build time, by the just-built `plang` compiler itself — `CMakeLists.txt`'s
`plang_shipped_crt_unit` target — rather than a hand-maintained prebuilt
object) alongside `Crt.pas`/`crt.tui`.

`shipped-rtl-crt-dos-strings-combined-utility.pas` (`test/Turbo/Units/`)
is the concrete, end-to-end proof this tier's own goal names directly: one
real program that `uses Crt, Dos, Strings` together and does something a
small real Turbo utility might — colors a banner line, lists real files in
a real directory with their real sizes, upper-cases a name through a real
`PChar` round-trip — built and run against the build-tree RTL (the only
tier lit itself can reach). The complementary "from a real INSTALLED
plang, no flags at all" half of that same claim — which lit cannot drive,
since a lit run always invokes the in-tree build-dir binary and has no
installed-prefix layout of its own — is exercised by CI's own "Check the
install rules" step (`.github/workflows/ci.yml`, both the Linux and macOS
jobs): a real `sudo cmake --install`, then a program identical in spirit
to the lit test above, compiled and run with `plang -std=turbo
usesrtl.pas -o usesrtl` alone, no `-I`, no `PLANG_UNIT_DIR`.

## Unit initialization sections do not run automatically

**This is the one deliberate, still-open scope cut this whole tier
carries.** A unit's `implementation` section may end with its own
`begin...end` (instead of a bare `end.`) — real Turbo Pascal runs every
`uses`d unit's own initialization section, in `uses`-clause dependency
order, before the program's own `begin` — but plang does not yet run it
automatically at all: only a **program's** own top-level `begin...end`
ever executes on its own. A unit compiled and run **standalone** (nothing
`uses`s it) is unaffected, since nothing about this gap touches how a
program's own body runs — but a unit's own runtime-computed globals and
any side effect its `begin...end` section would have produced simply do
not happen when another program or unit `uses`s it.

Confirmed directly, for this report: a unit
(`InitUnit`, exporting `var InitRan: Integer` and a `begin...end` section
that sets `InitRan := 99` and `Writeln`s a marker) compiled with `plang
-std=turbo -c` and linked into a program that immediately reads
`InitRan` — with **no** call of any kind into the unit first — prints
`InitRan=0`, not `99`, and the marker line never appears: the global
keeps LLVM's own zero-initializer, and the `begin...end` section's
`Writeln` never runs.

Every shipped unit that needed real initialization state worked around
this individually rather than waiting on it: `Crt` uses a lazy
`EnsureInit`, called from the START of every single exported
procedure/function, so state is set up the first time ANYTHING in the
unit is actually called (real TP code overwhelmingly calls something —
`ClrScr` is close to universal — before ever reading `TextAttr`/`WindMin`/
`WindMax` directly, so a bare, call-free FIRST read of one of those three
is the one narrow, separately-documented gap `EnsureInit`'s own pattern
cannot close). `Printer` bypasses the Pascal-level init path entirely with
a C++ global constructor that calls straight into the runtime. `Dos` and
`Strings` are mostly stateless (every call is a self-contained real
syscall or C string operation), so the gap does not matter to either.

This project's own test corpus deliberately does not — and should not —
assert that a used unit's initialization section runs: doing so would be
asserting behavior plang does not have. Every multi-unit integration test
in `test/Turbo/Units/` that needs a unit-level starting value (e.g.
`three-units-separate-compilation-real-string-pipeline.pas`'s own
`Counter`) sets it explicitly from the program, the same workaround
`a-unit-compiled-standalone-can-be-used-by-a-program-that-never-sees-its-
source.pas` already established. Closing this gap for real — running
every `uses`d unit's own initialization section, in dependency order,
automatically — remains open, unscoped future work, not part of this
tier.

---

## Object types

TP7's `object` model (not Delphi's `class` — no reference semantics, no
`try`/`except`, no automatic construction) is Tier 5. Single-module
declaration, inheritance, virtual dispatch, construction, and visibility
are Cluster A; cross-**module** object-type consumption (a program using
an object type declared in a `uses`d unit, inherited from and overridden
in a second) is Cluster B; a capstone integration test corpus and this
section's own "Known gaps" below are Cluster C. All three are complete —
everything below is implemented, cross-checked against a local `fpc -Mtp`
build throughout, and proven at integration scale by
`test/Turbo/Objects/` (single-module scenarios) and `test/Turbo/Units/`
(scenarios crossing a unit boundary). The per-item unit-level coverage
each paragraph below draws on lives under
`test/Parse/ParserTurboObjects/`, `test/Sema/SemaTurboObjects/`,
`SemaTurboConstructors/`, `SemaTurboObjectCompat/`, and
`test/CodeGen/CodeGenTurboObjects/`, `CodeGenTurboVirtualDispatch/`,
`CodeGenTurboConstructors/`, `CodeGenTurboObjectCompat/` — one directory
per Cluster A/B item, the same granularity Tier 4's own
`test/Parse/ParserTurboUnits/`/`test/Sema/SemaTurboUnitScoping/` split
already established.

**Declaration and inheritance.** `object [(Ancestor)] ... end`, with
`private`/`public` visibility sections (any number, in any order — TP7 has
no `protected`), `virtual`/`abstract` methods, and `constructor`/
`destructor`. Method bodies may be given out-of-line, dotted
(`TAnimal.Speak`), anywhere later in the same block. An object type must
have a name — `object ... end` used inline, with no `type Name = ...`, is
rejected (`err_object_type_anonymous`), since inheritance and dispatch need
something nameable to hang a VMT off of.

**Calls and fields.** `Obj.Method(args)`, `P^.Method(args)`, and the bare
no-parens form all resolve through the ancestor chain. `Obj.Field`/
`P^.Field` work both inside a method body (bare, unqualified — the
implicit `Self` every method body gets) and from ordinary code outside any
method (`A.Field`, `P^.Field`), reusing the exact same struct-offset
machinery a plain `record`'s own field access goes through — an object's
fields live in `RecordFields`, flattened ancestor-then-own, alongside a
record's.

**Virtual dispatch.** A `virtual` method is called indirectly, through the
receiver's own hidden `_vptr` field and a per-concrete-type VMT (one
`llvm::GlobalVariable` array of function pointers per type, built once and
memoized). `inherited MethodName(...)` / bare `inherited;` is always a
STATIC call to the direct ancestor's own body, never through the VMT — an
override calling `inherited` reaches its parent's implementation exactly
once, never redispatching. Calling an unoverridden **abstract** method
through the VMT (legal to leave unoverridden — TP7/FPC give only a
warning, "Constructing a class ... with abstract method ...", not an
error) traps at run time with **Runtime error 211** ("Call to abstract
method"), the same numbered code real Borland/FPC's own VMT slot traps
with — confirmed against a local `fpc -Mtp` build.

**Construction.** `New(P, Init(Args))` allocates, stamps the correct VMT
address into `_vptr`, and calls the constructor; `Fail` inside a
constructor sets a hidden per-activation flag `New` reads back to null `P`
on failure — no exception/unwinding machinery, matching TP7 (which
predates it). `Dispose(P, Done)` calls the destructor (through the VMT if
`virtual` — the usual idiom, so a caller holding only an ancestor-typed
pointer still reaches the real runtime type's own cleanup) and then frees.

**Object-type covariance (Cluster A item 7).** A descendant object VALUE is
assignment-compatible with an ancestor-typed variable (`A := D;` for
`A: TAnimal; D: TDog;`) — ordinary Pascal "object" value slicing, since
(unlike a `class`) an object is a value type and TDog's own storage begins
with TAnimal's own fields verbatim. A POINTER to a descendant is likewise
assignment-compatible with an ancestor-typed pointer (`PA := @D;`), and the
same covariance applies to both by-value and `var` parameters (`procedure
P(A: TAnimal)` / `procedure P(var A: TAnimal)` both accept a `TDog`
actual). All of this is one-directional only — the reverse (ancestor into
descendant) is refused, `cannot assign 'TAnimal' to variable of type
'TDog'` — confirmed against a local `fpc -Mtp` build throughout.

**Visibility.** `private` is enforced, but its real scope (confirmed
against `fpc -Mtp`) is the whole declaring **module** (the program or unit
that declares the object type), not the exact object type and not a TP7
`protected`-like descendant-only rule — TP7 has no `protected` at all. A
descendant type's own methods, and even ordinary code with no method
context whatsoever, may freely reach a private member as long as it is in
the same module; only code in a genuinely different module (reached
through `uses`) is refused. Now demonstrated end-to-end across a real
multi-file program (Cluster B item 8): a private field declared by an
ancestor object type in one unit is still counted — invisibly, at its real
offset — when a descendant declared in a *different* unit lays out its own
fields, but stays refused for genuinely unqualified name access from that
other unit, exactly the same `CurrentUnit_`-against-`DeclaringModule`
comparison the single-module case already used.

**`with anObjectInstance do`.** Opens the object's own fields unqualified,
through the identical with-scope mechanism (`pushWithScope`) the plain
`record` case already uses. Nests correctly with the implicit `Self` scope
a method body already has: a `with` block inside a method body, over a
*different* object instance than `Self`, exposes both at once — the
`with`-target's own fields unqualified (shadowing `Self`'s if a name
collides, ordinary innermost-scope-wins Pascal semantics), and `Self`'s own
fields still reachable by explicit `Self.Field` for any name shadowed this
way.

**`TypeOf`.** `TypeOf(anObjectValueOrTypeName)` returns the address of that
object's own VMT global (Turbo's generic `Pointer` type) — compared with
`=` to ask "is this really a TDog": `TypeOf(D) = TypeOf(D2)` is true for
two separate `TDog` instances, `TypeOf(A) = TypeOf(D)` is false for an
ancestor instance against a descendant one, and `TypeOf(anInstance) =
TypeOf(ATypeName)` both name the one global. Requires an object type with
at least one virtual method somewhere in its own hierarchy — `typeof can
only be used on object types with VMT` otherwise, FPC's own wording,
confirmed against a local `fpc -Mtp` build and reused verbatim. **Answers a
static, not a dynamic, question** — see "Known gaps" immediately below,
the most significant entry there.

### Known gaps

Four restrictions a user of this tier's `object` model can actually run
into, confirmed against a local `fpc -Mtp` build rather than assumed. The
first two below (`TypeOf`, `inherited`) are real bugs this capstone item's
own integration testing found while building the tests above — genuinely
new, not previously known or documented; the last two (bare no-parens
method calls, the by-value self-reference restriction) were already known
and are repeated here only so this section is a complete list, not scattered
across the document.

**`TypeOf` resolves its argument's STATIC type, never its dynamic one.**
The paragraph above is accurate as far as it goes, but leaves out a real
gap: `TypeOf`'s own CodeGen lowering (`CGFuncCall.cpp`, search "TypeOf(x)")
picks the VMT from `Args[0]`'s Sema-resolved static type — by explicit
design, the same unevaluated-operand treatment `SizeOf`/`High`/`Low` get —
never from the object's own `_vptr` field at run time the way virtual
dispatch itself (`P^.SomeMethod`, unaffected by this) does. This is
invisible, and correct, whenever a variable's static and dynamic types
coincide — every case above, and
`test/Turbo/Objects/typeof-distinguishes-directly-typed-instances-across-a-three-level-hierarchy.pas`,
only ever exercise that case. It gives a wrong answer the moment they
diverge: `TypeOf(P^)` for `P` an ancestor-typed pointer actually holding a
descendant instance — the textbook use for a "what does this really point
to" facility, and the one no test written before this capstone item tried.
Real `fpc -Mtp` reads the dynamic type correctly there; plang does not.
Pinned, not fixed (a real fix is CodeGen work, out of scope for a test/docs
item), at
`test/Turbo/Objects/typeof-through-an-ancestor-typed-pointer-answers-statically-not-dynamically-known-gap.pas`.

**`inherited` is a statement, never an expression.** Real Turbo Pascal
routinely calls an inherited *function* and uses its result directly —
`Result := inherited GetValue + 1;` is ordinary TP7 object code, confirmed
here against a local `fpc -Mtp` build. plang's parser only ever reaches
`TokenKind::Inherited` from its statement dispatch (`ParseStmt.cpp`,
building an `InheritedCallStmt`) — there is no expression-level production
for it at all, so `S := inherited Describe;` and even the fully-parenthesized
`S := inherited Describe();` both fail to parse (`expected expression, got
'inherited'`). Calling an inherited method and merely discarding its
result — `inherited Describe;` as its own statement, even when `Describe`
is a function — works fine and is exactly how every multi-level chain in
this tier's own tests (including this capstone's own
`test/Turbo/Objects/` scenario) is written. Not pinned as its own test
here — a parser rejection does not fit this suite's own "real compiled and
run, with observed output" convention the way a wrong-but-running answer
does — but written down here so it is not a surprise.

**A bare (no-parens) method call does not resolve in expression
position.** `Obj.Method(args)`/`P^.Method(args)` and a bare, argument-free
call *in statement position* (`D.Bark;`, no parens, item 5's own idiom)
both resolve correctly. A bare, argument-free method call used **as a
value** — `writeln(GetLegs)`, `x := Self.GetLegs`, `x := GetLegs` inside
the method's own body relying on the implicit `Self` — does not: Sema
reports `object type 'TFoo' has no field 'GetLegs'` (qualified) or
`undefined identifier 'GetLegs'` (bare), even for a directly-declared,
non-inherited, single-file method with no units or inheritance involved at
all. Every test in this tier routes around it with explicit parens
(`GetLegs()`) — the workaround real Turbo Pascal never actually needs, but
plang currently does.

**A record/object type cannot be its own by-value parameter type before
its own declaration finishes.** `procedure Copy(Other: TFoo)` inside
`TFoo`'s own declaration is refused — a pre-existing forward-reference
limitation general to `record`/`object` alike, not specific to this tier,
unaffected by this capstone item. Route around it with a pointer or `var`
parameter instead.

One more, genuinely cosmetic, not counted above: a `method ... hides the
inherited method of the same name` warning false-positives for same-named,
differently-signed, non-virtual constructors declared at different levels
of one hierarchy (the standard `Init`-at-every-level TP7 idiom) —
confirmed a local `fpc -Mtp` build stays silent on the identical
construct, but the warning is otherwise harmless, does not affect codegen,
and every constructor test in this tier (including this capstone's own)
compiles with it uncommented-on. This capstone item also observed the
identical false positive surface at a cross-unit **call site** rather than
only at a declaration
(`test/Turbo/Units/three-units-cross-unit-new-fail-dispose-lifecycle-and-virtual-destructor-dispatch.pas`)
— still the same same-named-constructor false positive, just reached
through the cross-unit "declare if missing" path instead of a direct
declaration, not a second issue.

---

## See also

- `plang(1)` — the full command-line reference, including `-d`/`-u`/`-Fi`/`-frange-checks`.
- [`docs/conformance.md`](conformance.md) — the ISO 7185 clause 5.1 documentation for the base language.
- [`docs/modules.md`](modules.md) — Extended Pascal modules and separate compilation.
- `include/plang/Basic/CompilerSwitches.def`, `lib/Lex/Directives.cpp` — the source of truth Tier 1's directive-system sections above were written from.
- `include/plang/AST/TypeContext.h`, `include/plang/Sema/Type.h`, `lib/Sema/SemaType.cpp` — the sized-integer ladder, narrowed subrange/enum storage, and `SizeOf`/`byteSizeOf`/`byteAlignOf`'s layout arithmetic Tier 2's type sections above were written from.
- `include/plang/Basic/Builtins.def`, `runtime/plang_sstr.cpp`, `runtime/plang_val.cpp` — the System-unit string routines' and `Val`'s exact, empirically-derived semantics, including the `fpc -Mtp` field-practice citations this document summarizes.
- `runtime/plang_file.cpp`, `runtime/plang_math.cpp` — Tier 3's file model (`Assign`/`Reset`/`Rewrite`/`Append`/`Close`/`BlockRead`/`BlockWrite`/`Seek`/...), `InOutRes`/`IOResult`, and `Random`'s own empirically-derived semantics, including the `fpc -Mtp` field-practice citations this document's Tier 3 section summarizes.
- `test/CodeGen/Turbo/`, `test/Driver/Turbo/` — per-feature regression coverage for everything in this document.
- `test/Turbo/` — integration-level tests exercising realistic COMBINATIONS of Tier 3's file-model/`InOutRes`/`Random` features together (a program that opens a file, does I/O, checks `IOResult`, and exits cleanly — the kind of end-to-end scenario a real Turbo Pascal program actually runs), rather than one lit test per isolated behavior.
- `lib/Sema/Sema.cpp` (`pushUnitUsesScopes`, `loadUnitInterfaceExports`), `lib/Driver/Driver.cpp` (`scanUsesClauseUnitNames`, `findShippedUnitObject`), `lib/Basic/UnitSearchPath.cpp` — Tier 4's scoping, separate-compilation, and shipped-RTL search-path mechanisms this document's Tier 4 section was written from.
- `share/plang/units/` — the shipped `Crt`/`Dos`/`Printer`/`Strings` unit sources, each with its own detailed header comment on exactly what it does and does not reproduce from real Turbo Pascal 7 / `fpc -Mtp`.
- `test/Turbo/Units/` — Tier 4's own integration-level test corpus: realistic multi-unit, multi-file COMBINATIONS (last-unit-wins shadowing across three real units, mutual implementation-`uses` doing real bidirectional work, three-unit separate compilation with sources deleted, the shipped RTL used all together) rather than one lit test per isolated behavior — the same split `test/Turbo/`'s own Tier 3 capstone already established for Tier 3; also now home to Tier 5's own cross-unit object-model integration tests (Cluster B item 8's inheritance/virtual-dispatch capstone, and Cluster C's own New/Fail/Dispose-lifecycle companion), reusing the same directory rather than inventing a Tier-5-specific one.
- `lib/CodeGen/CodeGenProcs.cpp` (`getOrCreateVmt`, VMT slot assignment, `emitAbstractMethodStub`, and the per-constructor `curCtorOkAlloca` success-flag setup), `lib/CodeGen/CGProcCall.cpp` (`emitBoundMethodCall` — `New`/`Init`, `Dispose`/`Done` — and `Fail`'s own codegen), `lib/CodeGen/CGFuncCall.cpp` (`TypeOf` lowering), `lib/Sema/Sema.cpp`/`SemaExpr.cpp`/`SemaStmt.cpp`/`SemaType.cpp` (`resolveObjectType`, `VmtSlotEntry`, object-type resolution, the VMT slot table, visibility, covariance), `lib/Parse/ParseStmt.cpp` (`InheritedCallStmt`) — the source of truth Tier 5's "Object types" section above, including its own "Known gaps", was written from.
- `test/Turbo/Objects/` — Tier 5's own single-module integration-level test corpus (Cluster C): a 3-level "employee roster" hierarchy combining virtual dispatch through an ancestor-typed pointer, multi-level `inherited` chains, the full `New`/`Fail`/`Dispose` construction lifecycle, and virtual-destructor dispatch in one realistic program, plus dedicated `TypeOf` coverage (both its correct, narrow usage and the ancestor-typed-pointer case it gets wrong, pinned rather than fixed) — the same split `test/Turbo/Units/`'s own Tier 4 capstone already established for cross-unit scenarios, applied here to Tier 5's single-module ones.
