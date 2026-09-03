# Changelog

All notable changes to plang are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [0.4.0] - 2026-09-02

### Added

- **`-static`/`-dynamic` driver flags**, dynamic linking now the default.
  Previously the driver only ever knew how to link the plang runtime
  statically, by embedding `libplang.a`'s own path directly in the link
  command; there was no dynamic auto-link path at all, so a user who wanted
  one had to pass `-lplang` by hand, which only worked because the *build*
  host's linker default search path happened to include `libplang.so`.
  `-dynamic` (the new default) instead passes `-L<libdir> -lplang` plus a
  matching `-rpath <libdir>` (the absolute install libdir, not `$ORIGIN`-
  relative -- the produced binary is the user's own program, built wherever
  they choose, not something installed alongside the compiler), so the
  resulting binary can find `libplang.so`/`.dylib` again at run time
  without `LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH`. `-static` opts back into
  the old self-contained-binary behavior. Verified with real compile-link-
  run tests for both modes, plus an `-rpath` check via `readelf -d` (issue
  #805).

### Fixed

- **[P0] A fresh install on a `lib64`-multilib distro (Fedora, openSUSE,
  Arch-family systems) could not link any program at all.** `findRuntimeLib`
  (the driver's runtime-archive lookup) hardcoded the literal `"lib"` when
  building the installed-layout candidate path, but the install rules put
  the runtime at `CMAKE_INSTALL_LIBDIR`, which GNUInstallDirs resolves to
  `"lib64"` (not `"lib"`) on exactly those distros. The driver silently
  found no runtime, silently linked without it, and the link only failed
  several layers down with a raw, driver-diagnostic-free `ld.lld: undefined
  symbol: plang_set_args` et al. -- exactly what issue #805's reporter hit.
  Fixed by baking the real configured `CMAKE_INSTALL_LIBDIR` value in as
  `PLANG_INSTALL_LIBDIR` (mirroring the existing `PLANG_RUNTIME_DIR`
  build-tree fallback) and looking there instead of a hardcoded `"lib"`.
  Reproduced and verified against a real `cmake --install` done with
  `-DCMAKE_INSTALL_LIBDIR=lib64` directly matching the report.
  Additionally, a genuinely missing/broken runtime (as opposed to merely
  hard to find) now gets a clear `err_runtime_not_found` diagnostic instead
  of the same confusing raw linker failure -- mirroring the existing
  `-sanitize-runtime` diagnostic for its own missing-build case -- except
  in linker-only mode (issue #611: a `plang a.o b.o -o out` invocation with
  no `.pas` source at all), where nothing could need the plang runtime in
  the first place and the check is skipped entirely (issue #805).

## [0.4.0] - 2026-09-02

### Added

- **`-std=turbo`: unit `implementation ... begin ... end` initialization
  sections now run automatically**, in real dependency order, before a
  program's own body -- previously they never ran at all except when a
  unit was compiled and executed as its own standalone translation unit.
  Each unit's own init function now calls its direct dependencies' init
  functions first (depth-first, `uses`-clause order); the existing
  per-unit idempotent guard (already shipped for Extended Pascal module
  initializers) makes any repeat call anywhere in the dependency graph a
  no-op, so the correct transitive order falls out of ordinary recursion
  with no separate graph/topological-sort pass needed. Mutual `uses`
  through `implementation` (already supported for interface resolution)
  is handled deterministically by compile order: whichever of the two
  units compiles second is the one whose init calls the other's, since
  only it resolves the other through a real `.tui`. Proven across a real
  separate-compilation boundary (a unit compiled standalone via `-c`,
  its source deleted, then linked into a program that never sees the
  source) as well as a "diamond" dependency (two units sharing a common
  third dependency, confirmed to initialize exactly once) (issue #790).
- **`-std=turbo`: `{$IFOPT switch+}`/`{$IFOPT switch-}`.** Tests the CURRENT
  state of a compiler switch at the point it appears -- `{$R+} ... {$IFOPT
  R+}` takes its branch, a later `{$R-}` flips it -- reusing the existing
  `CondFrame`/`{$ELSE}`/`{$ELSEIF}`/`{$ENDIF}` machinery `{$IFDEF}`/
  `{$IFNDEF}` already had, and `Scanner::CurrentSwitchState` (falling back to
  `Opts.defaultSwitches()` before any switch directive has run) that
  `SwitchTable` was built to answer in the first place. Only the letter
  spelling is accepted, matching `fpc -Mtp`: a long-name switch, or a switch
  with no letter at all (`ObjectChecks`/`Goto`), is a real, reported error
  (`err_directive_ifopt_bad_switch`) rather than a silent branch (issue
  #794).
- **`-std=turbo`: `const` parameters, untyped parameters, and open-array
  parameters.** `procedure P(const x: SomeRecord)` reads like an ordinary
  parameter but may not be assigned to (`err_const_param_assigned`,
  parallel to but distinct from EP's protected-parameter mechanism); a
  structured actual (record/array/set) is passed BY REFERENCE for the
  efficiency the feature exists for, rather than copied in the way an
  ordinary value parameter still is. `procedure P(var x)` -- no type at
  all -- is the classic memcpy/memcmp idiom: Sema rejects every use of `x`
  except as the operand of a variable typecast or a direct relay to
  another untyped formal, both confirmed against a local `fpc -Mtp`
  build. `procedure Sum(a: array of Integer): Integer` is Turbo's own
  open-array form (not Extended Pascal's conformant-array syntax, which
  stays rejected under `-std=turbo` and vice versa) -- any length, always
  zero-based inside the callee (`Low(a) = 0`, `High(a)` = the actual's own
  element count minus one) regardless of what bounds the actual was
  itself declared with, reusing EP's existing conformant-array machinery
  (ptr+bounds calling convention, by-value copy-on-modify) rather than a
  new mechanism.
- **`-std=turbo`: `PChar`/`PAnsiChar`, pointer arithmetic, and `p[i]`
  indexing.** `p + n`, `p - n`, `p1 - p2` (an element count), `p[i]` as
  both a read and a write, and a zero-based `array[0..n] of Char`
  decaying to its address with no `@` needed (`p := buf`). Gated
  structurally on "pointer whose pointee is Char" rather than nominally on
  the name `PChar`, matching real `fpc -Mtp` field practice verified
  directly against `fpc` 3.2.2: a user's own `type P = ^Char` gets
  exactly the same arithmetic and indexing PChar does, on a real Turbo
  compiler, not only the predefined name. ISO 7185 and Extended Pascal
  `^char` are unaffected (`=`/`<>` only, ISO §6.7.2.5); the gate is
  `Opts.turbo()` everywhere this applies, checked independently at each
  of the three call sites (`Sema::checkBinary`, `Sema::checkIndex`,
  `Sema::isAssignCompatible`). ISO §6.4.3.2's canonical `array[1..n] of
  char` string-type is untouched and does not decay: only a 0-based array
  does, matching fpc's own refusal of the 1-based case.
- `-std=turbo` now declares the sized-integer ladder (`ShortInt`, `Byte`,
  `SmallInt`, `Word`, `LongInt`, `Cardinal`, `LongWord`, `Int64`, `QWord`),
  `AnsiChar` and the untyped `Pointer` type -- the foundation the rest of
  Tier 2 is written against. `SmallInt`/`Integer` and `LongWord`/`Cardinal`
  are literally the same interned type (`TypeContext::getInt` keys on width
  and signedness alone), which a diagnostic now reflects accurately instead
  of always saying "integer" for every one of them: `getInt` names each
  freshly-minted width/signedness pair from the ladder unless it is the
  dialect's own unqualified `integer`, which keeps its plain name unchanged.
  `Pointer` is assignment- and comparison-compatible with any other pointer
  type, in either direction, matching real Turbo Pascal's untyped pointer.
  None of the eleven names are available under `-std=iso7185` or
  `-std=iso10206`.
- **`-std=turbo`: `Random`, `Random(Range)`, `Randomize`, `RandSeed`, `Int`,
  and `Frac`.** `Random` alone answers a `Real` in `[0, 1)`; `Random(Range)`
  answers an integer-kind value in `[0, Range)`, staying in `Range`'s own
  type the same way `Abs`/`Sqr`/`Succ`/`Pred`/`High`/`Low` already do --
  `Sema::checkCallExpr` special-cases the dual arity/dual-result shape the
  same way it already does for `Abs`/`Sqr`. Both the bare, no-parens
  spelling (`x := Random;`) and the parenthesized zero-argument one
  (`Random()`) are supported. `RandSeed` is a new predefined, settable
  32-bit (`LongInt`-width, matching real Turbo Pascal's own declaration,
  independent of `Integer`'s own dialect width) global holding the
  generator's state, registered the same predefined-mutable-`Var` way
  `ExitCode` already is. `Randomize` reseeds it from wall-clock time
  (`clock_gettime(CLOCK_REALTIME)`). The generator itself is a small,
  self-contained 32-bit linear-congruential generator, hand-rolled because
  the runtime is built without the C++ standard library's `<random>` --
  it is plang's own sequence, matching neither real Borland Turbo Pascal
  7's own LCG nor Free Pascal's Mersenne Twister, and no claim is made that
  it does. `Int(x)`/`Frac(x)` take and return a `Real`: `Int` is `x`'s
  integer part toward zero (like `Trunc`, but with no `int64` range to
  respect, since the result is a `Real`), and `Frac` is `x - Int(x)`. Built
  as new, wholly unrestricted `std::trunc`-based runtime functions rather
  than reusing the existing, range-checked `Trunc`/`Round` primitives (which
  would have silently reintroduced their `int64` range check, aborting for
  any `Int`/`Frac` argument with `|x| >= 2^63`).
- **`-std=turbo`: heap error handling, the exit chain, and program-control
  argument access.** `GetMem`/`FreeMem` are a wholly separate, NON-ABORTING
  pair of allocation entry points from `New`/`Dispose` (which keep aborting
  the process unconditionally on out-of-memory): with no `HeapError`
  installed, a failing `GetMem` returns `nil` rather than halting the way
  real Borland's own default does -- a deliberate, documented divergence,
  chosen so a plang Turbo program can check `GetMem`'s result without
  installing a handler at all. `HeapError` is a settable procedural VALUE
  (reusing Tier 2's procedural types/values rather than a new mechanism);
  returning 1 from it makes `GetMem` return `nil`, anything else reports
  Runtime error 203. `RunError(code)` turned out to already exist from
  earlier work. `ExitProc`, another settable procedural value, is hooked
  into the already-working `plang_module_finals_run`/`plang_halt` chain
  (issue #242) so it runs on `Halt`, on `RunError`, and on normal program
  termination alike. `ErrorAddr` is deliberately simplified: set only at
  `RunError` and at a nonzero-status `Halt`, not wired to every individual
  runtime-fault call site. Every compiled program's C `main` now takes
  `(argc, argv)` unconditionally, for every dialect -- ISO 7185 and
  Extended Pascal programs never read them, but a single, dialect-
  independent entry-point ABI is what lets `ParamCount`/`ParamStr(n)` read
  real command-line arguments back via a new `plang_set_args`, called as
  main's own first instruction.
- **`-std=turbo`: `Assign`/`Append` and TP's own file model.** `Assign(f,
  name)` binds a file variable to an external filename (or, for an empty
  name, to "the console" -- confirmed against `fpc -Mtp`: a following
  `Reset` then reads stdin, a following `Rewrite`/`Append` writes stdout);
  `Reset`/`Rewrite`/`Append`/`Close` under `-std=turbo` take no filename
  argument of their own, unlike ISO/EP's `reset`/`rewrite`, and instead
  open whatever a prior `Assign` bound the file to. Backed by a genuinely
  separate `plang_tp_assign`/`plang_tp_reset`/`plang_tp_rewrite`/
  `plang_tp_append`/`plang_tp_close` runtime family (`runtime/
  plang_file.cpp`), dispatched at the call site (`CGProcCall.cpp`) rather
  than a shared function branching on a passed-in dialect flag -- ISO's
  `reset`/`rewrite`/`close` are untouched and still reached for every other
  dialect. `FileMode: Integer` (default `2`, matching real Turbo Pascal) is
  now a second predefined mutable global, following `ExitCode`'s own
  mechanism exactly. The shared `PascalFile` struct (`plang/Basic/
  PascalFileLayout.h`) gained three fields every dialect's file variable
  now carries -- `Name` (the bound filename), `Mode` (TP's
  `fmClosed`/`fmInput`/`fmOutput`/`fmInOut`, real Borland-documented values
  confirmed empirically against a local `fpc -Mtp` build) and `RecSize` (an
  as-yet-unused placeholder a later item wires up for `Reset`/`Rewrite`'s
  record-size argument) -- appended at the end, so no existing field's
  offset moves and ISO 7185/Extended Pascal's own file behavior is
  unchanged (verified by comparing `-emit-llvm` IR text before and after
  across a sample of existing ISO/EP tests: identical but for the
  struct-literal type's own spelling growing to match, confirmed
  mechanical by substituting the old spelling for the new one in the
  "before" IR and diffing again).

### Fixed

A ninth adversarial review round, the largest yet: this project's own 6-way
parallel sweep across every completed Turbo tier and the EP/ISO 10206 core,
plus three external models run independently against the same codebase
state, together filed 143 confirmed bugs. Triaged by severity (29 P0, 67
P1, 51 P2/P3) and fixed in waves of up to 5 concurrent worktree-isolated
agents -- a root-cause fix, a regression test, and a full Debug+Release
rebuild-plus-test-suite pass per cluster, then one more independent
verification pass per wave (re-running every original repro from scratch
and hunting for interaction bugs between the wave's own fixes) before
moving on. That last discipline is what pushed the total past 143: fixing
bugs kept surfacing more of them. A squash-merge of two sibling PRs once
left `main` briefly non-compiling (two fixes had each refactored the same
function's local variables on different, non-conflicting lines -- caught
and fixed within minutes, not through any single PR's own CI); an
independent verification pass caught a fix for one bug (#616) leaving a
gap that produced a fresh crash on a case its own author never tested
(#723); fixing a bare-method-call resolution gap (#773) introduced a
silent-wrong-answer regression on a name-clash case a different,
already-merged fix (#730) existed specifically to prevent, caught by
re-running that fix's own regression tests against the new one (#781); and a full
26-bug pass thought complete after four separate rounds of triage was
still missing 9 real P0s that had simply never been graded, caught only
by a stray tally while wrapping up an unrelated P1 campaign. Every one of
these is fixed as part of the entries below rather than left for a future
release.

- **Compiler crashes and ICEs**, now clean diagnostics or correct codegen
  instead: a record with a scalar field ahead of a Turbo `string[N]` field
  (wrong hardcoded alignment in the runtime layout walk, #591); `for x in
  setExpr do` when `x` already denotes an outer Set-typed variable (#588);
  `Assign`/`Rename` with a packed-array-of-char filename (#671); a typed-
  file `write` under `-std=turbo` (#683); a `ShortString` function result
  relayed through a procedural parameter/variable (#684); a record
  structured value constructor as a value argument, and selectors on one
  (#685); `FindFirst` with an integer-literal attribute mask (#696); a
  bare `inherited;` after a static hide with a mismatched signature
  (#616, plus a gap in that very fix caught by independent re-verification
  and closed same-round, #723); `FillChar`/`Move` with a negative `Count`
  (#628); reading an untyped `var` parameter through a typecast (#645);
  calling a nil procedural variable (#646, now a clean Runtime error 216
  trap); `Dispose(P, Done)` on a nil `P` segfaulting the same way instead
  of trapping (#579); schema-backed string initial state and value-argument passing
  (#606, #607); a procedure-local object type (#617, now a clean
  diagnostic); a qualified, no-parens call to a parameterless method
  returning `string` (#786); and a procedural variable's indirect call
  mismarshaling a `const` record parameter -- a crash when the record had
  a `set` field, a silent wrong answer when it didn't (#772).
- **Silent miscompilation and data corruption** -- the most severe class,
  fixed with no observable diagnostic previously at all: Turbo
  `Integer`/`Byte` arithmetic silently skipping 16-bit wraparound whenever
  either operand was a literal (also flipping comparison results, #577,
  with a Byte-width gap and an inline-use regression in the first fix
  caught and closed by the same round's own re-verification); mixed
  signed/unsigned comparisons and arithmetic wrong at every width below 64
  bits (#629, #630); `Abs`/`Sqr` exposing their internal runtime-helper's
  own result width instead of the operand's real declared type (#609); `Assign`/`Reset(f,0)`/`Rewrite(f,0)` leaving a
  previous stream live so writes landed in the wrong or deleted file
  (#573, #587); an unqualified reference resolving to the *first* `uses`d
  unit instead of the last, violating Turbo's own documented shadowing
  rule (#594); set union/symmetric-difference silently dropping members
  across mismatched base windows (#681); cross-unit `inherited` calls
  mismarshaling `var`/set parameters (#682); Turbo's mutating builtins and
  variable-typecast assignment silently bypassing `const`/protected
  parameter checks (#710, #711, plus the same gap in a parser-level
  `const var` combined prefix and in `readstr`/`writestr`, #712, #714);
  and a bare/qualified parenthesis-free call to a method sharing a name
  with an inherited field silently reading the stale field instead of
  calling the method (#781, a regression introduced by this round's own
  #773 fix and caught the same round).
- **Turbo object model (methods, VMT, `inherited`)**: an unqualified call
  to a sibling method from inside another method's own body, or from a
  `with obj do` block, failed to resolve (#571, #623); method-call
  resolution was keyed by a bare name instead of the receiver's own
  resolved type identity, giving wrong results under type-name shadowing
  (#621); `inherited` to an abstract-only ancestor method compiled clean
  and only trapped at runtime instead of at compile time (#574); a cross-
  unit unoverridden abstract method failed to link (#618); `New` used as
  a function expression, the canonical polymorphic-allocation idiom, was
  rejected (#622); a bare `inherited;` in a root object with no ancestor
  was rejected (#624); a function-method's implicit result assignment was
  shadowed by an inherited field of the same name (#626); a statement-
  position method call on a function-result pointer didn't parse (#627);
  `inherited` inside a nested procedure within a method was rejected
  (#625); VMT identity broke across unit boundaries, so `TypeOf` on the
  same declared type answered differently depending which unit asked
  (#619); re-virtualizing a statically-hidden method took over the old
  VMT slot instead of a fresh one, dispatching to the wrong implementation
  (#620); a bare, argument-free method call used as a value (not just in
  statement position) failed to resolve in expression context (#773); and
  virtual dispatch trapped after a plain `New(p)` followed by an ordinary,
  non-extended-syntax constructor call, since only the extended `New(p,
  Ctor(...))` form stamped the VMT (#780).
- **Turbo file I/O and `IOResult`/`InOutRes` semantics** -- a large
  cluster, all cross-checked against real `fpc -Mtp` field practice:
  `Close` never checking whether the file was actually open (#575);
  `Append` on a nonexistent file silently creating it (#576); a negative
  radix-prefixed integer failing to parse from a text file (#592);
  `ExitProc` chaining -- a handler reassigning `ExitProc` from inside
  itself -- not honored (#595); a typed-file `Read` at or past EOF
  recording no error even under `{$I+}` (#661); the Turbo text model
  treating only LF as a line marker and substituting a space for CR
  (#662); genuine write failures (`ENOSPC`, ...) misreported as a fixed
  "file not open" code instead of the real underlying error (#663);
  `Flush` only ever able to report one fixed result regardless of what
  actually happened (#664, plus its file-kind restriction being
  documented backwards, #739); `BlockWrite` suppressing genuine write
  errors and misreporting direction violations (#665); a stale one-
  character lookahead surviving `Truncate`, `BlockWrite`, and typed
  `Write` (#666); a text-file `Reset` honoring `FileMode` when real
  Turbo/`fpc` never let `FileMode` touch `Text` files at all (#667,
  independently rediscovered and closed as a duplicate, #735); typed/
  untyped `Rewrite` being effectively write-only, with `Eof` always
  `TRUE` immediately after (#668); `Eof` on typed files computed byte-
  wise instead of record-wise (#669); a negative literal token wrapping
  silently into `Word`/`Cardinal`/`LongWord` (#672); a negative `RecSize`
  being a silent no-op instead of trapping (#679); `Seek(f, n)` reporting
  stale or garbage `IOResult` -- including false success -- when `n *
  RecSize` overflowed a 64-bit integer (#583); `Reset(f)` with an out-of-
  range `FileMode` opening read-write instead of falling back to read-
  only (#589); `Truncate(f)` on a read-only-opened file reporting the
  wrong `IOResult` code (#593); `Append` accepted on typed/untyped files
  when it should be Text-only (#670); and, the deepest gap
  in this cluster: `{$I-}`'s documented "a pending, unread `InOutRes`
  suppresses every further I/O call, on any file, until `IOResult` is
  read" contract was only ever implemented for `Eof`/`Eoln` -- every
  other I/O entry point kept working normally while an error sat pending,
  a gap only reachable once this round's own fixes made errors reliably
  *detectable* in the first place (#738).
- **Turbo unit system (linking, search order, naming)**: a case-variant
  spelling of a unit export failing to link because Sema and CodeGen
  mangled the symbol name differently (#694); enum constants of a unit-
  exported enum type left undefined in any importer (#695); no compile-
  time conformance check between a unit's interface heading and its
  implementation body (#698); `uses System` rejected (#699); a local
  `.pas` losing to a same-named shipped `.tui` (#700); transitive unit
  objects not auto-linked (#705, plus the fix itself initially missing
  any normally-capitalized unit name, only ever trying an all-lowercase
  object filename, caught and closed the same round, #746); a failed
  unit compile leaving its freshly-published `.tui` behind (#706); a
  corrupt `.tui` in an earlier search directory hard-erroring instead of
  falling through to a later one (#707); the driver's `.o` resolution
  disagreeing with Sema's own `.tui` resolution about which directory a
  unit actually came from (#708); duplicate and self `uses` silently
  accepted (#709); a type cast through a unit-imported type name not parsing as a cast
  (#701); and Dos exporting Delphi/SysUtils-style prefixed
  attribute constants instead of real TP7's unprefixed names, never
  returning `.`/`..` from `FindFirst`/`FindNext`, plus nested
  `FindFirst`/`FindNext` loops corrupting each other's directory-
  scan state (#581, #582, #697).
- **Sized-integer, cast, and pointer semantics**: `Hi`/`Lo`/`Swap`
  mis-folding integer literals at 64 bits instead of their real operand
  width (#631); `Ord` zero-extending a signed narrow type instead of
  sign-extending (#632); `Boolean(x)` as a value cast keeping only the
  low bit instead of treating any nonzero value as true (#633); `PChar`/
  `PAnsiChar` typecast expressions not parsing as casts at all (#634);
  `Hi`/`Lo`/`Swap` rejecting a subrange-typed argument (#635); literal-
  literal string comparison using EP's space-padding rule under
  `-std=turbo` (#636); a global `absolute` aliasing a component (not a
  bare variable) crashing instead of a diagnostic (#639); pointer
  relational comparison rejected under `-std=turbo` (#640); a string
  literal assigned to a 0-based char array rejected (#641); loose-`Bool`-
  to-integer conversion zero-extending where real `fpc` sign-extends
  (#642); a misleading `{$R+}` string-index range-check message that
  excluded a legal index-0 (#643); `x in s` never range-checking under
  `{$R+}` (#637); `-std=turbo` incorrectly accepting negative-based sets
  (#692); `set of Byte` incorrectly rejected as exceeding the 256-element
  set limit (#580); `minint div -1` trapping unconditionally under
  `-std=turbo` when real `fpc -Mtp` itself never traps it (#638); string
  literals and `@wholearray` rejected as `PChar` arguments (#702); and
  `PChar` pointer subtraction truncated to a 16-bit result and wrapping
  (#713).
- **EP/ISO 10206 correctness**: schema-instantiated set types rejected by
  every set operator despite being independently assignable (#584);
  structured value constructors rejecting every schema type with a
  misleading "type not found" (#590); `empty(f)` implementing "positioned
  at EOF" instead of the standard-mandated "has no components" (#686); a
  conformant packed-char-array parameter rejecting string literals
  (#687); `dispose` of a schema/variant instance accepting standard-
  mandated errors and silently dropping discriminant expressions (#688);
  the `for...in` control variable being implicitly shadow-declared
  instead of naming an ordinary, already-declared variable, contra
  §6.9.3.9.1 (#689); the two-word `and then`/`or else` spellings rejected
  in favor of only this project's own non-standard underscored ones
  (#690); dynamic (runtime-computed) schema discriminants rejected for
  local variables while the heap-allocation form already accepted them
  (#691); and a substring-range assignment/read on a `protected`
  parameter bypassing the protection check (#586).
- **Parser, lexer, and directive robustness**: two more stack-headroom-vs-
  term-count recursion-guard siblings to the ones fixed in earlier rounds
  -- `Sema::resolveType` had no guard at all (#596), and `Sema::checkBlock`
  /`checkProcBody` crashed at a strikingly ordinary ~2.5 MiB of stack
  (#597) -- plus a third, `parseFactor`'s own parenthesized-expression
  ceiling, which had a term-count check but no stack-headroom one (#572);
  a Turbo cast-parsing scope leak letting a local variable's name affect
  cast recognition outside its own procedure (#599); Turbo `^letter`
  character constants failing to parse after `=` and `of` (#600); a
  dead-branch conditional skipper fooled by `{$...}`-looking text inside
  string literals or comments (#644); a dead conditional branch unable to
  straddle an `{$I}` include boundary (#651); whitespace changing a one-
  letter Turbo directive's meaning (#604); a directive placed mid-
  statement not applying to the rest of that statement's own checks
  (#655); comments/directives unable to span an include boundary (#656);
  nested includes lacking `fpc`'s own current-working-directory fallback
  (#657); the comma-separated `{$R+,I-}` multi-switch form applying
  neither switch and warning misleadingly (#658); and `{$J-}` not
  actually making a Turbo typed constant immutable (#603).
- **Procedural variables**: a procedural variable couldn't itself be
  passed as the actual argument to a procedural-typed parameter (#647); a
  call through a procedural-typed array element didn't parse (#648); and
  a bare reference to a function-typed procedural variable in value
  context was rejected, treated like the auto-calling rule for a plain
  function name instead of a value read (#649).
- **Driver, diagnostics, and runtime misc**: `-c` linking object-only
  input into an executable instead of stopping after producing the
  object (#611); multi-input `-c` silently discarding every object after
  the first (#612); `dump-parse-tree` exiting successfully despite a
  promoted error (#613); malformed UTF-8 shifting later diagnostic
  columns and emitting raw bytes into diagnostic text (#614); `Assert`
  rejecting a short-string variable as its message (#601); `RunError`,
  `Delay`, and `Halt` all accepting a real-valued argument (#602, #653);
  `RunError`/`Halt`'s `ExitCode`, printed message, and actual process
  exit status disagreeing for codes outside 0-255, three different
  truncation widths that were never reconciled (#775); `ExitProc`
  observing a stale `ExitCode` after `RunError` (#652); no compile-time
  diagnostic for an out-of-range constant for-loop limit or assignment,
  and that diagnostic not covering built-in ranged types like `Byte`
  once it did exist (#654, #776); Turbo for-loops rejecting an assignment to the control variable within
  the loop body, though real TP7 allows it (#650); `warn_for_var_after_loop`
  firing under
  `-std=turbo` though Turbo leaves the control variable well-defined
  after a normal loop exit (#659); an unchecked out-of-bounds write (under
  `{$R-}`, already-UB territory) able to corrupt the RTL's own exit-state
  globals, hardened by consistently truncating and reusing one canonical
  value for both the printed message and the real exit code rather than
  two independently-truncated ones (#660); `SizeOf`/`High`/`Low` falsely warning
  "read before given a value" despite never evaluating their argument
  (#578); a spurious warning on `BlockRead`/`BlockWrite`'s `amt` out-
  parameter and `GetMem`'s `p` (#673); `Read(f)` with no value arguments
  on a typed file silently accepted as a no-op (#674); a field width past
  `INT32_MAX` aborting unconditionally even under `{$I-}` (#675);
  `Random(Range)` with a negative `Range` always returning 0 instead of a
  negative value (#676); fixed-decimal formatting of large reals printing
  the exact binary expansion instead of a reasonable rounding (#677);
  `docs/turbo.md` incorrectly claiming `InOutRes` isn't a nameable
  identifier (#678); a char read at EOF returning 0 instead of Ctrl-Z
  (#680); Crt leaving the terminal in raw mode on a fatal signal, and
  `WhereX`/`WhereY` not tracking actual `Write`/`Writeln` output (#703,
  #704); `get`/`put`/`close`/`page` silently ignoring extra or missing
  arguments instead of a compile-time arity error (#605, #693); PMI
  serialization dropping `bindable` type metadata (#608); nested same-
  name nominal types incorrectly treated as assignment-compatible
  (#598); and `-std=turbo` wrongly rejecting a function returning a
  record, array, or set type, all of which real `fpc -Mtp` allows (#585,
  #787).
- **Closing four more documented gaps between `-std=turbo` and real
  `fpc -Mtp`**, found and fixed in the same pass that produced the
  `{$IFOPT}` and unit-initialization work above: a record/object type
  can now name itself as a by-value or `var` method parameter before its
  own declaration finishes (#791); a `method ... hides the inherited
  method of the same name` warning no longer false-positives on a non-
  virtual redeclaration -- it now only fires when the ancestor
  declaration being hidden was itself `virtual`, matching real `fpc`
  exactly (#792); an anonymous inline enum used as a `set`'s (or `file`
  of...'s) base type no longer fails to link, since enum-constant
  registration now recurses into `Set`/`File`/`Pointer`/`Packed` type
  nodes the same way it already did for `Array`/`Record` (#774); and an
  integer literal between `Int64`'s maximum and
  `QWord`'s own maximum is now accepted wherever the destination context
  is unsigned-compatible, instead of being rejected outright regardless
  of destination (#795, plus a subrange/array-index-bound context the
  first fix missed, giving a misleading error instead of an accurate one,
  closed the same round, #800).

- `Sema::checkIdent`'s generic `SymbolKind::Builtin` case never checked
  whether the current dialect actually has the name in question -- only
  `checkCallExpr`'s `checkEPOnly` call, reached only through the
  parenthesized-call grammar, did. This was latent (every other
  dialect-restricted builtin function either takes at least one required
  argument, so a bare, parenthesis-less use of it was already just an
  ordinary undefined-identifier or link failure, or, like `eof`/`eoln`, is
  declared in every dialect and has no gating to skip) until `Random`
  above became the first dialect-restricted, *zero-argument* `Func`
  builtin: `x := Random;` under `-std=iso7185`/`-std=iso10206` silently
  compiled and ran `Random`'s own generator instead of being refused the
  way the parenthesized `Random(5)` already correctly was. `checkIdent`
  now calls `checkEPOnly` too.
- `-std=turbo` accepted the buffer-variable dereference `f^` for a `File`-
  typed variable with no diagnostic at all (`Sema::checkDeref`'s `File` arm
  had no dialect check whatsoever), even though real Turbo Pascal has no
  buffer variable -- it replaces ISO 7185's whole get/put/page file-buffer
  model with `Assign`/`Seek` instead. Both a read (`x := f^`) and a write
  (`f^ := ...`) position are now refused under `-std=turbo`
  (`err_turbo_file_buffer_var`); `-std=iso7185` and `-std=iso10206` are
  unaffected, and so is an ordinary `^SomeType` pointer dereference or an
  Extended Pascal schema-typed pointer's own dereference form under any
  dialect -- the new check is scoped to `PtrTy->Kind == TypeKind::File`
  alone, a branch structurally separate from both.
- `get`/`put`/`page`/`pack`/`unpack` refused under `-std=turbo` (correctly
  gated in `Builtins.def` since they are the other half of the same
  file-buffer model `f^` belongs to, above) reported "is an Extended Pascal
  extension and is not available under -std=iso7185" -- backwards twice
  over: these five are ISO 7185's own required procedures, not an Extended
  Pascal extension, and the dialect actually refusing them was `-std=turbo`,
  not `-std=iso7185`, whichever one happened to be hardcoded into the
  message. `Sema::checkEPOnly` now picks among four dialect-aware
  diagnostics by asking `Builtins.def`'s own dialect mask which dialect(s)
  a name actually belongs to, rather than assuming: a new
  `err_turbo_file_model_name` ("'get' is part of Pascal's file-buffer model,
  which -std=turbo replaces with Assign and Seek") for this group; a
  reworded `err_ep_required_name` ("only available under -std=iso10206",
  dropping the wrong assumption that `-std=iso7185` is always the dialect
  asking) for Extended-Pascal-only names like `card`, refused under either
  `-std=iso7185` or `-std=turbo`; and a new `err_ep_turbo_required_name` for
  the two names (`Halt`, `Length`) both Extended Pascal and Turbo have,
  which only `-std=iso7185` can ever refuse.
- `writeln`/`write` treated any 8-bit integer as a `Char` (`BuiltinIO::
  writesAsChar` matched on LLVM width alone before consulting the Sema
  type), which was unreachable before the sized-integer ladder above since
  no `Integer`-kind type was ever 8 bits wide -- `ShortInt` and `Byte` are,
  and were printed as raw (usually unprintable) bytes instead of their
  decimal value. Now checked against the Sema type's own `Kind` whenever
  one is available.
- Turbo constant-expression folding now bounds-checks against the expression's
  own (possibly narrow) resolved type instead of always assuming 64-bit
  `Integer`: Tier 1 shipped `checkedAdd`/`checkedSub`/`checkedMul`/
  `checkedNeg`/`isoPow` (`Arith.h`) as width-generic, but neither Sema's
  constant folder (`SemaType.cpp`) nor CodeGen's (`ConstFold.cpp`) ever
  actually passed a width, so `const Big = 30000 + 30000;` under `-std=turbo`
  compiled with no diagnostic and silently gave `Big` the 64-bit sum
  truncated to 16 bits (`-5536`) instead of refusing a value 27233 past
  Turbo's `Integer` range. It is now a compile-time error; `30000 + 2000`
  (in range) still folds cleanly. ISO 7185 and Extended Pascal, whose one
  `Integer` type is always 64-bit, are unaffected.
- `write`/`writeln` of a `QWord` past `Int64`'s own range printed as
  negative (`plang_write_i64`/`plang_write_file_i64` format with `%PRId64`
  unconditionally, and CodeGen never consulted the value's signedness at the
  write-dispatch site), and `read`/`readln` of one rejected it outright
  (`plang_read_i64`'s `strtoll` reports the same range error a malformed
  token gets). Every narrower unsigned rung (`Byte`/`Word`/`Cardinal`/
  `LongWord`) is zero-extended to i64 before reaching a writer and so never
  disagreed with the signed formatter; `QWord` is the one rung wide enough
  to. New `%PRIu64`/`strtoull`-based entry points
  (`plang_write_u64`/`plang_read_u64` and their `_w`/`_file`/`_turbo`
  variants) now back a `QWord` destination end to end, including its full
  `18446744073709551615` maximum.
- `read`/`readln` of a `ShortInt` or `Byte` variable read exactly one raw
  character and used its ASCII code as the value (`read(b)` on `"200"` read
  `'2'` and stored 50), rather than parsing the token as a number:
  `BuiltinIO::readFnSuffix` picked the reader from the LLVM type alone, and
  an 8-bit ordinal was indistinguishable from a `Char` at that level --
  unreachable before the sized-integer ladder, the read-side counterpart of
  the `writesAsChar` write-side bug fixed above. Now checked against the
  Sema type's own `Kind`, the same fix already applied to the write side.
- `Single`'s default (no field-width) `write`/`writeln` showed double-
  precision noise past its own ~9 significant digits: `3.14159265358979`
  stored in a `Single` printed as `3.1415927410125732E+000` instead of a
  value capped to what a 32-bit float actually holds. CodeGen promotes a
  `Single` to `double` before formatting (there is only one formatter), which
  is exact for the bits the double already has, but showing all seventeen of
  a double's default significant digits also shows the promotion's own
  zero-padding-turned-nonzero tail as if it were part of the original value.
  A field-width write with no decimals clause (`s:24`) had the identical
  problem, growing into the same noise instead of padding. Both now cap at
  nine significant digits and pad a wide field with leading spaces instead of
  inventing precision, matching `fpc -Mtp` field practice; a fixed-decimals
  write (`s:0:8`) is unaffected, since the requested decimal count already
  bounds the output.
- `write`/`writeln`/`read`/`readln` of a Turbo `string[N]` (`ShortString`) to
  or from a file variable reached an explicit internal-error abort
  ("ShortString file I/O is not implemented yet") instead of working the way
  every other string kind's file I/O already does. New PascalFile-aware
  `plang_sstr_write_file`/`plang_sstr_write_file_w`/`plang_sstr_read_file`
  runtime entry points (mirroring `string(N)`'s own `plang_str_write_file`
  family, minus its eight-byte length header) now back it.
- `write`/`writeln` of a `PChar`/`PAnsiChar` value was rejected outright
  ("`'PChar' cannot be written`"), even though CodeGen's write dispatch
  already handles a bare pointer value correctly by treating it as a
  null-terminated C string -- `Sema::checkCallStmt`'s write-parameter
  whitelist never grew a case for it. Now accepted (gated to `-std=turbo`,
  matching `isCharPointerType`'s other call sites), printing the bytes a
  `PChar` points to up to its terminator, confirmed against `fpc -Mtp`.
  `read`/`readln` of a `PChar` remains rejected, matching `fpc -Mtp` (a
  pointer alone carries no buffer capacity for a reader to respect).
- The parser's typecast-target pre-seeding (`Parser::Parser`, added for the
  sized-integer ladder) never covered the Boolean-family variants or
  `Single`, registered by the very next `Opts.turbo()` block in
  `Sema::registerBuiltins`: `ByteBool(x)`/`WordBool(x)`/`LongBool(x)`/
  `Single(x)` all failed to parse as a cast at all ("`'ByteBool' is not
  callable`"). Since a loose Boolean's whole point is holding a
  non-canonical nonzero value, and assigning a plain integer to one is
  itself rejected, a working cast was the *only* way to construct one --
  this was not merely a convenience gap.
- `lib/Frontend/Frontend.cpp`'s `typeDenoterToString` (used by the `.pmi`
  module-interface writer) always serialized a bounded string type as
  `string(N)` (EP's `VarString` syntax) regardless of `IsShortString`,
  silently mis-serializing Turbo's `string[N]` as if it were EP's
  `string(N)` -- a different type with a different binary layout. Now checks
  `IsShortString` and emits `string[N]`, matching `AstPrinter.cpp`'s existing
  convention. `-std=turbo` and EP's module system are structurally mutually
  exclusive today, so this had no live call path to reach, but is fixed on
  the same "do not leave a silently-wrong serialization in place" grounds as
  the rest of this project's AST/type-denoter printing.
- A NAMED array type declaration (`type A = array[1..5] of Integer;`) was
  interned exactly like an anonymous one (`TypeContext::getArray`, keyed on
  index/element/packed alone), so two separately-declared array types of
  identical shape shared one `Type*` and were silently interchangeable --
  including through a `var` parameter, where ISO §6.6.3.3 requires strict
  identity: `procedure P(var x: B); ... var a: A; P(a)` compiled and ran for
  two unrelated types `A`/`B` of the same shape. `record` and
  enumerated-type declarations already got the correct treatment (ISO
  §6.4.2.3: each declaration is a distinct type, so neither is ever
  interned); a named array now gets it too, minted through
  `TypeContext::makeArrayUncached` and renamed/un-anonymized by the
  identical `nameNominalType` call Enum/Record already go through -- one
  unique `Type` per declaration, with `Sema::isAssignCompatible`'s `Array`
  arm now requiring the same declared-name identity its `Record` arm
  already does. An array type-denoter written inline, with no `type`
  declaration of its own, is unaffected and remains exactly as structurally
  compatible as it always was; only a NAMED array's own identity changes.
  (#178)

## [0.3.5] - 2026-08-27

A sixth adversarial review round, and by far the largest: three external models,
independent of this project's own review process, each did a full pass and filed
what they found as GitHub issues rather than reporting back directly. 119 confirmed
bugs (roughly 6x the size of any prior round), triaged into six waves by severity and
subsystem and fixed with the same discipline as rounds 1-5 -- a fail-before/pass-after
regression test per fix, a real `gdb` session (not just IR-text checks) for anything
touching debug-info emission, and a full rebuild plus the complete test suite before
every merge. That last discipline caught real bugs no individual PR's own CI could
have: merging two independently-correct fixes together produced a genuine null-pointer
segfault (a `for`-loop bound check that stopped gating on symbol kind once a sibling
fix changed the surrounding code), a stale test assertion (a codegen-level test
expecting the exact ICE text a newly-merged Sema-level check now intercepts earlier,
with a better diagnostic), and a `.pmi` filename casing mismatch (one fix's test
predated a sibling fix's later, deliberate switch to lowercased filenames). All three
were caught by this project's own full-suite reverification before merging, not by
any agent's or any PR's own CI, and are fixed as part of the commits below rather than
filed as separate issues. `--target`'s fix (#243) also surfaced a real, previously
latent bug purely by making cross-compilation possible for the first time: Sema's
`byteSizeOf` hardcoded the host's 8-byte pointer width unconditionally, invisible
until a non-host target made CodeGen's real `DataLayout` genuinely disagree -- fixed
in the same PR, verified with a live `gdb` session on the ordinary native case to
confirm zero regression to the common path.

### Fixed

- **Compiler crashes, ICEs, and undefined behavior**, now clean diagnostics or correct
  codegen instead: `new()` on a non-pointer type (#206); a for-loop control variable
  that isn't a variable (#205); a builtin procedure called where a value is expected
  (#222) or with the wrong arity in statement position (#207); `ord`/`chr`/`odd` of a
  non-ordinal argument, which used to crash Sema's own CodeGen instead of erroring
  (#212); `chr()` not range-checking its argument at all (#166); singleton subranges
  (`lo == hi`) never getting a range check (#195); a variant tag field name that
  duplicates a fixed field (#208); a file type nested inside an array or record
  component (#167); a zero-size record rejected as a file's component type (#241); a
  parameter's array index type not routed through the same checker as everywhere else
  (#258); patching forward-declared pointers inside array/file element types (#209); a
  non-local `goto` from a module procedure to a module-block label (#211); a stack-
  overflow-shaped crash from unbounded recursion in EP structured-value constructors
  (#203) and in the constant folders, which also gained checked arithmetic instead of
  silently wrapping on overflow (#201, #202, #204); checked arithmetic for array
  extent/size computation, closing the same wraparound-causes-heap-corruption bug
  pattern for local variables too, not just globals (#214, #215, #223); a nondecimal
  literal's base overflowing past the 2..36 range check (#213); `sqr(x)` on an
  out-of-range integer silently wrapping instead of trapping (#219); extent-form `mod`
  using raw `srem` instead of ISO's sign convention (#228); every `<cctype>` call in
  the scanner now casts to `unsigned char` first, closing real UB on a negative
  `char`/non-ASCII byte (#221); for-loop bound-type checking and threat-scan gaps,
  including a real segfault this round's own merge introduced and caught before it
  reached `main` (#259, #265, #291); threading the Sema record into the two remaining
  `layoutOf` callers that were missing it (#197).
- **Runtime memory-safety and undefined behavior**: `emitCStrArg`'s C-string buffer
  was sized from a static probe instead of the actual runtime length (#216); a source
  file's size was `stat`-ed after reading instead of before (#218); `fseek`'s return
  value went unchecked in `SeekRead`/`SeekWrite`/`SeekUpdate` (#233); `rebindPointer`
  left a stale placeholder cache entry behind (#288); `substr_assign`'s past-the-end
  check could overflow the same way `substr`'s already-fixed one did (#220); `for ...
  in` could rebind a mismatched control variable's storage (#217); three more
  varying-string operations walked their access path twice, the same double-evaluation
  bug pattern fixed once already (#196); `read()` checked its own arguments twice
  (#272).
- **Sema correctness -- silent wrong-acceptance and missing diagnostics**, the largest
  single group this round: `isAssignCompatible`'s blind spots that accepted when they
  should have rejected (#171, #172); `with`-statement now requires an lvalue and
  refuses restricted (schema-body) records (#264, #290); `schemaInstMatch` compared
  spelling instead of declaration identity (#255, #268); `halt` resolved by name
  instead of by symbol identity, and a dereferenced pointer was wrongly marked written
  (#270, #271); incomplete forward-declared procedures went unaudited, and a type-error
  placeholder could fake a stub (#266, #269); var-strings were checked against their
  declared capacity instead of their actual runtime length (#231, #232); parameters,
  locals, and program-parameters were cross-checked for collisions in only one region
  (#289, #292); variant-tag and case-constant validation gaps (#253, #257, #260, #293);
  value clauses and discriminant range-checking in `new()` on a schema (#194, #230);
  conformant-array bound/index type checking (#262, #263, #267, #294); a fixed schema
  instance's discriminant read narrowed to its declared type (#210); a set window's
  rebase/width machinery had three separate gaps (#225, #226, #227); `read`/`readln`
  targets were never checked for assignability (#224); `string(n)` capacities were
  routed through a probe/255 fallback instead of the real declared capacity (#193,
  #198); packed-record alignment was ignored for `with`-bound and indexed fields
  (#192); a file's buffer variable didn't get the alignment codegen already promised it
  elsewhere (#199); a narrower right-hand side wasn't widened to its destination slot
  in `emitAssign` (#229); a value clause's constant wasn't range-checked against its
  type's bounds (#254); `write`'s field-width and decimals expressions went
  type-unchecked (#256); a subrange or array-index bound pair could mix two unrelated
  ordinal types (#251); `pack`/`unpack` didn't validate their operand element types or
  packedness (#252); required-function arguments were validated only for a handful of
  special-cased names (#261).
- **Runtime and library behavior**: `arg(0+0i)` returned 0 silently instead of
  trapping the undefined phase angle (#249); `binding(f).bound` went false merely
  because `f` had been closed, not because it had actually been unbound (#248); a
  nested `writestr` corrupted the enclosing capture through shared global state
  (#235); `halt()` skipped module finalisers instead of running them (#242); `reset`/
  `rewrite` with no explicit name failed to reuse the last one that was given, and
  `reset` of a directory wasn't rejected (#239, #287); a char-typed `reset`/`rewrite`/
  `extend`/`update` file name wasn't marshalled the way every other string-shaped
  argument is (#296); bare CR line endings weren't
  indexed as their own line, and diagnostic columns counted bytes instead of display
  cells (#285); the string/boolean field-width writers had no shared overflow guard
  (#247); a sticky `ferror` misattributed a later, successful operation to an earlier
  failure (#238); numeric text-input parsing had four separate gaps, including
  spinning forever on malformed input instead of trapping cleanly (#236, #237, #240,
  #284); a named text file's final partial line was never terminated on close (#234);
  the message-catalog ABI check wrapped at 2^32, and a `msgctxt`-less entry past the
  first was misread as the header (#280); `.pmi` files are now canonicalized
  (lowercased, so module lookup is properly case-insensitive), published atomically,
  and parsed more defensively (#168, #173, #175); `-dump-ast` no longer writes `.pmi`
  files as a side effect of a read-only inspection mode (#274); three diagnostics were
  declared but never actually emitted anywhere, now removed with a lint to catch the
  next one (#295); the compiler's own output-write failures (a full disk, `/dev/full`)
  now report a nonzero exit instead of silent success (#246); `AstPrinter` dropped
  set-constructor type names, value clauses, `ResultName`, and a module's
  interface/implementation kind (#273); GCC/Clang toolchain version directories were
  sorted lexicographically instead of numerically, so `9` could beat `10` (#250);
  filenames and locale tags are now control-character-escaped before reaching a
  terminal or a log, closing a terminal-escape/log-injection hole (#281).
- **Driver, CLI, and the `-pc1` front end**: `--target` never actually reached the
  frontend, so a cross-compiled build silently kept the host's triple and data layout
  (#243, plus a latent Sema pointer-width bug this uncovered, see above); `Driver::run`
  called `exit()` directly for `--version`/`--help`/etc. instead of returning control
  to an in-process caller the way its own header promises, and `-c` silently ignored
  linker-only inputs instead of warning (#174, #277); `-o`'s joined form
  (`-ofile.ll`), `-L`/`-l`'s separate form, and an empty `-o` were all handled
  inconsistently, and the `-###`/`-v` command echo wasn't shell-quoted (#244, #245,
  #286); the front end's own CLI diagnostics bypassed `-w`/`-Werror`/`-Wno-<name>` by
  printing straight to `stderr`, and `-pc1` had no directory guard of its own (#275,
  #276); only a subset of the project's public headers were actually installed,
  breaking anything that transitively included one that wasn't (#169); multi-file
  output/temp-file management had three gaps: collision-prone output filenames,
  temp files leaking on a signal, and stray `.o` litter in the working directory
  (#170, #278, #279); release tags are now checked against `VERSION` and the shared
  library's own embedded metadata before a release ships (#185).
- **Documentation, CI, and build tooling**: `.clang-format` had two keys clang-format
  22 rejects outright, breaking the formatter for every contributor (#187);
  `PLANG_SANITIZE` was a `BOOL` `option()` instead of a validated `STRING`, so a typo'd
  sanitizer name configured silently and only failed deep in build output (#186);
  `docs/technical_info.md`'s test counts and `test/README.md`'s FPC differential-
  testing description had both drifted well out of sync with what actually exists
  (#188); `docs/conformance.md`'s `succ`/`pred` entry predated a since-added range
  check, and its integer-overflow entry didn't mention that EP's `pow` operator and
  `minint div -1` both trap unconditionally (#282, #283); CI never actually failed
  when `lit` or `FileCheck` were unavailable -- a missing tool silently registered zero
  test suites while `ctest` still reported 100% success (#184).

## [0.3.4] - 2026-08-27

A fifth adversarial review round, prompted directly by unease about 0.3.3's own new `-g`
schema debug-info mechanism -- a broad sweep at the same scale as rounds 1-4, with 6 of 14
lenses specifically targeting that mechanism. 19 confirmed bugs, several exactly the
"confidently wrong, not honestly incomplete" failure mode the unease was about. One more real
bug was found and fixed independently while merging: two of this round's own fixes (the
sidecar identity redesign and the nested-schema-field bail-out) each passed CI and looked
correct in isolation, but landing them together left a stray empty sidecar entry behind on a
bail-out path neither fix's own tests exercised in combination -- a reminder that CI on each
PR alone cannot catch a bug that only exists once two independent changes coexist; caught and
fixed as part of this round's own merge, not filed as a separate numbered issue.

### Fixed

- **Silent wrong values (worst class), all in the 0.3.3 `-g` schema debug-info mechanism**:
  the sidecar was keyed by bare schema NAME only -- two different procedures declaring a
  same-named-but-differently-shaped schema collided, with the second's live objects printing
  the first's wrong field names/values, no warning (#140). The sidecar had no staleness check
  at all -- rebuilding the same source (even without `-g`) silently overwrote the one sidecar
  file every previously-built `-g` binary from that source depends on (#141). Both fixed by
  keying sidecar entries on `(name, structural fingerprint)` and embedding a random build ID
  in both the sidecar and the binary's own `DW_AT_producer`, checked before trusting the
  sidecar. A schema `var`/value parameter's ABI pointer is body-relative, not header-relative,
  but both DWARF and the sidecar assumed header-relative unconditionally -- wrong values for
  every field on one of the most common debugging actions (#142). A field typed as a nested
  schema instantiation wasn't excluded by the recorder's bail-out logic -- wrong data or a
  crash instead of a clean bail (#143). Non-integer field types (real/set/complex) had no type
  tag in the sidecar -- printed as raw bit-pattern integers, or crashed outright for 16-byte
  complex fields (#144, which also hardens the script against malformed/unexpected sidecar
  content more generally).
- **Documented-but-overclaimed limitation**: the 0.3.3 fix only ever corrects WHOLE-value
  printing (`print q^`) -- a direct field-path access (`print q^.tail`) never invokes gdb's
  pretty-printer at all, an architectural gdb API limitation, not fixable by more plang code.
  The script now warns about this prominently at load time (#145).
- CodeGen's own recursive expression emitter had no depth guard (unlike Sema, fixed in #123)
  -- an expression well under Sema's cap could still crash under the project's own ASan CI
  build, since ASan's larger per-frame stack usage drops the real crash threshold below Sema's
  (#146).
- `SchemaLayoutEngine::rtSizeOfTypeNode`'s array-FIELD-within-a-record byte-count multiply
  (the more common shape) had no overflow guard, reproducing the same wraparound-causes-heap-
  corruption bug an earlier fix only closed for a schema's own top-level array body (#147).
- `plang foo.pas -o foo.pas` silently destroyed the source file with no diagnostic -- gcc and
  clang both reject this; plang now does too (#148).
- A schema discriminant of the wrong ordinal type was silently accepted when instantiating a
  schema directly (`var v: Vec('a')`) -- only `new()`'s discriminant path had the type check
  (#149).
- File-open failures (`reset`/`rewrite`/`extend`/`update`) crashed with `std::abort()`
  (SIGABRT, core dump) instead of the clean exit status every sibling runtime-error path uses
  (#150).
- A leading UTF-8 BOM produced three bogus per-byte lexer errors instead of being skipped
  (#151).
- The wrong-mode file-write/read trap added for named files never fired for INTERNAL
  (unbound/temp) files, since the C-level `tmpfile()` behind them is always bidirectional
  regardless of the Pascal-level intended direction -- the same silent-corruption class of bug,
  left open for the arguably more common internal-file case (#152).

## [0.3.3] - 2026-08-27

A fourth adversarial review round, prompted by "really shake bugs out" before starting the
Turbo Pascal extensions -- 14 lenses, including a dedicated adversarial re-review of round 3's
own brand-new `-g` debug-info code, file I/O edge cases, driver/CLI flag combinations, module
diamond-import stress under genuine separate compilation, and a second opinion on the
versioning code from earlier the same day. Eleven bugs (one independently found by three
separate lenses at once -- a strong signal), every fix carrying a regression test verified to
fail against the code it fixes. One fix's own first draft introduced a real regression
(broke ordinary enum-literal resolution), caught by independent verification before merge and
corrected in the same PR.

### Fixed

- **A schema's DIType under `-g` was missing its runtime discriminant header**, corrupting
  every field's apparent value under a debugger (not just an approximated extent for the
  varying field, as previously documented -- every field, including the discriminant itself,
  read completely wrong). Fixed in two parts: the DIType itself now gets the discriminant
  header right, making the discriminant and every field at or before a varying-extent field
  exact; a field declared *after* a varying one has no correct static DWARF offset possible at
  all (confirmed directly from LLVM's own DWARF emitter -- a computed member address has no
  implementation there, only a narrower bitfield-offset feature that crashes gdb if misused for
  this), so a companion gdb Python pretty-printer (`share/plang/gdb/plang_schema_printers.py`)
  is shipped instead, computing the correct value from live memory at print time and
  bypassing DWARF's limitation entirely for `print`ing the whole value. **Caveat added in
  issue #145: this only covers WHOLE-value printing (`print q^`)** -- a direct field-path
  access (`print q^.field`) never invokes the pretty-printer at all, since gdb's
  pretty-printer API only intercepts formatting of an already-resolved value, never
  sub-expression evaluation; such an access is resolved by gdb's own evaluator straight off
  the (potentially wrong) DWARF offset and can still read incorrectly for a field after a
  varying one. This is a fundamental gdb API limitation, not fixable with more plang code;
  the script now warns about it prominently at load time, and its docstring and the README
  state it plainly.
- A long flat chain of same-precedence binary operators (tens of thousands of terms, no
  parentheses) overflowed the stack in Sema's recursive expression walk, crashing with a raw
  SIGSEGV instead of a diagnostic -- the existing depth guard only covered parenthesized
  nesting, never a flat operator chain.
- `write()` to a file opened via `reset()` (read-only) silently discarded the write; `read()`
  from a file still open write-only from `rewrite()` returned stale caller-memory garbage --
  both from unchecked `fwrite`/`fread` return values.
- `plang` couldn't link pre-compiled `.o`/`.a` files without at least one `.pas` source,
  breaking the standard compile-then-link workflow every C toolchain supports.
- A directory passed as the input file produced six misleading cascading parser errors instead
  of one clear diagnostic.
- A diamond-imported module failed to initialize in the correct order (or at all, in some
  cases) when compiled as genuinely separate translation units, even though the single-file
  case already worked correctly.
- The version-string fallback path (used with no reachable release tag, e.g. a shallow clone)
  disagreed with the primary path on whether untracked files count as "dirty".
- `warn_unused_variable`/`warn_unused_parameter` pointed at the shared type token, not the
  actual identifier, for multi-name declaration groups (`a, b: integer`).
- A non-ordinal array index produced two overlapping diagnostics for one root cause.
- A bare-identifier reference to a required constant (`pi`, `maxint`, ...) redeclared as a
  function returned the builtin instead of calling the user's function, inconsistent with
  explicit call syntax which was already fixed for this class of bug.

## [0.3.2] - 2026-08-27

A third adversarial review round, prompted by the goal of making plang "bulletproof" before
starting the Turbo Pascal (0.4.0) extensions -- 14 lenses, deliberately targeting ground the
first two rounds left thin: deep `-g` interaction with every other feature, an `-O1`/`-O2`/`-O3`
sweep, fresh second opinions on the runtime and CodeGen decomposition, string/ordinal edges,
cross-module boundaries, feature combinations, diagnostic quality, and resource/scale stress
testing. Eighteen bugs, every fix carrying a regression test verified to fail against the code
it fixes.

### Fixed

- **Composite-typed locals and parameters were completely invisible under `-g`.** Every type
  except the seven scalar/pointer kinds -- record, array, set, complex, string/`VarString`,
  procedure/function, schema/schema-instance -- silently got no DWARF variable at all, despite
  `-g` shipping as a complete feature in 0.3.0. Real `DIType` construction added for every kind,
  verified with real `gdb` sessions per kind, not just IR-text checks.
- A procedural-parameter thunk had zero DWARF info, so stepping through a call made via a
  procedural parameter silently ran to completion instead of entering the thunk or the real
  target.
- An EP module's imported global got a spurious duplicate `DW_TAG_variable` in the importing
  compilation unit.
- Integer `**`/`pow` silently wrapped on overflow instead of trapping, the same class already
  fixed for `div`, missed for `pow`.
- `write`'s `FracDigits` (`:D`) argument had the same missing-range-check gap already fixed for
  `TotalWidth` (`:W`).
- A string literal with an embedded NUL byte silently truncated.
- EP's free declaration order broke for a `const` referencing an enum value declared in a
  later type section.
- Constants exported from one module and used in a genuinely-separately-compiled third module
  produced an undefined external symbol at link time.
- A module with only an interface part and no implementation part compiled clean with broken
  output instead of a Sema error.
- An unqualified import shadowing a builtin/required identifier was silently dropped, keeping
  the builtin instead.
- Fixed-discriminant schema-instance parameter types were never congruous with themselves,
  rejecting legitimately identical forward declarations.
- String concatenation `+` accepted one string-like operand without checking the other,
  crashing CodeGen instead of diagnosing a real type mismatch.
- Passing a dialect-gated-off required identifier as a procedural-parameter actual gave a
  misleading diagnostic about the wrong cause.
- Record type layout was O(n²) in field count, taking multiple minutes to compile at tens of
  thousands of fields.
- A global array large enough to overflow a 32-bit PC-relative relocation produced a confusing
  internal `ld.lld` error instead of a clean diagnostic.
- Case-statement range-label duplicate checking enumerated every value in a range with no
  bound, hanging the compiler on one large range.
- Negative zero's sign was inconsistently suppressed across real-formatting modes; the default
  real-write precision could round-trip `DBL_MAX` to `+Infinity`.

## [0.3.1] - 2026-08-27

A follow-up adversarial review of 0.3.0, focused on the ground the previous pass hadn't
covered: the new `-g` support, a fresh skeptical re-check of the CodeGen decomposition
itself, and areas with no prior scrutiny at all -- sets, complex numbers, file I/O, non-local
`goto`, procedural parameters, and a systematic sweep for missing range checks. Nineteen bugs,
several of them real memory-safety issues. Every fix carries a regression test verified to
fail against the code it fixes.

### Fixed

- **`-g` combined with any of `-O1`/`-O2`/`-O3` crashed `llc` outright, on ordinary code.**
  Three or more levels of nested procedures, where a non-innermost level's own local or
  parameter reuses the name of a variable captured from further out (an extremely common
  pattern -- reusing `i`, `x`, `count` at multiple nesting depths), produced a DWARF scope
  shape LLVM's own source languages never generate: a subprogram parented directly under a
  lexical block that is itself inside another subprogram. `llc`'s DWARF backend segfaulted
  building the abstract subprogram context for it, and `-O0` never reached the same code path,
  so this shipped invisibly until it hit code slightly more nested than what the original
  shadowing fix (0.3.0) had tested.

- **Conformant array parameter accesses had no range check at all.** Every other way of
  indexing an array in plang -- a plain array, one behind a pointer, a schema array -- is
  range-checked; the one path handling EP §6.6.6.2 conformant array parameters was not,
  so an out-of-bounds index silently read or wrote past the actual allocation, even with
  range checks on by default. Found independently by two separate review passes.

- **A non-local `goto` out of a procedure with its own by-value conformant-array parameter
  leaked that parameter's heap copy permanently.** The copy's disposal only ran on the
  normal-return path; the `setjmp`/`longjmp`-based non-local unwind skipped it entirely.
  Confirmed with a real AddressSanitizer/LeakSanitizer before-and-after.

- **`minint div -1` — the one integer division that overflows even with a nonzero divisor —
  crashed with an uncontrolled SIGFPE**, or gave a silently wrong answer depending on
  optimization level. Now traps cleanly, matching this runtime's existing convention for
  other undefined arithmetic.

- **Unbounded recursion in type, statement, and nested-procedure-declaration parsing
  stack-overflowed the compiler.** A depth guard already existed for deeply nested
  parenthesized expressions (0.3.0); it covered only that one construct. Deeply nested array/
  record/pointer/set/file types, deeply nested compound statements, and procedures declared
  inside procedures declared inside procedures all crashed the same way. All three now report
  a clean "too deeply nested" diagnostic instead.

- **An overflowing `-ferror-limit=` value crashed the compiler** with an unhandled
  `std::out_of_range` instead of a diagnostic naming the bad flag value.

- **An untyped set literal spanning more than 256 elements silently dropped elements with no
  diagnostic**, when every element was non-negative. `x in [0, 300]` read `false` for 300 --
  silently wrong, not merely unchecked -- because the width check added for a *negative*-lower-
  bound literal was never reached for a purely non-negative one. `card([0, 300])` and similar
  now report the same "exceeds the 256-element limit" error a negative-bound literal already
  got.

- **Complex division used the naive `(ac+bd)/(c²+d²)` formula**, silently producing a wrong
  (finite, non-NaN) result whenever the divisor's magnitude squared over- or underflowed
  double range, even though the true quotient was perfectly representable. Replaced with
  Smith's algorithm (the same approach glibc's own complex-division routine uses), which
  never squares the larger-magnitude component outright.

- **`reset`/`rewrite`/`extend`/`update` passed a `VarString` struct pointer directly as the
  runtime's filename argument**, instead of its actual character data, whenever the filename
  expression was a var-string under Extended Pascal -- silently corrupting or colliding file
  names between calls with different filenames.

- **`round()`/`trunc()` of a real outside the representable `int64` range silently returned a
  garbage sentinel value instead of trapping.** `round(1e30)` returned a specific wrong
  integer with no indication anything had gone wrong; now reports a clean domain error.

- **`-Wl,<args>` and `-Xlinker <arg>` were parsed but never actually forwarded to the linker
  invocation** -- accepted on the command line, silently doing nothing.

- **`write(real:W:D)` with a very large runtime-computed `W` produced gigabytes of garbage
  output, or silently dropped the write**, instead of the field-width diagnostic every other
  width form (integer, char, boolean -- 0.3.0) already raises. The real-with-width path had
  been missed when that guard was added.

- **Complex `**` skipped the domain-error guard the real `**` path already has** for a zero
  base with a non-positive-real-part exponent (EP §6.8.3.2), silently producing `Inf`/`NaN`
  instead of the runtime diagnostic the mathematically identical real case already gives.

- **`sqrt()` of a negative real silently returned `NaN`** instead of trapping, unlike this
  codebase's own established convention for `abs`/`pow`/`ipow` domain errors. `ln()` of a
  non-positive real had the identical gap and is fixed alongside it.

- **A module interface's `.pmi` dropped the `import` clauses its own declarations depend
  on**, and separately, **a qualified imported name (`M.name`) could never be used to denote a
  type at all** -- only in expression or statement position. Together these meant ISO 10206
  §6.11.6's own Example 3 (an interface heading using a type imported, qualified, from
  another module) could not be compiled, split across files or not. A third, related gap in
  implementation-module import scoping was found and fixed making that example actually pass
  end to end.

- **Value parameters of a subrange type were never range-checked against the formal's
  declared subrange** at the call site, unlike an ordinary out-of-range assignment to a
  subrange-typed variable, which already is.

- **Set constructor elements were never checked against the set's declared base-type
  range** before being admitted into the runtime bitmask -- a constant out-of-range element
  is now a compile-time diagnostic, and a runtime-computed one a dynamic range check, matching
  how every other kind of out-of-range value in a set constructor is already handled.

### Fixed (build and test infrastructure)

- **Every `lit`-based test suite shared one `test_exec_root`**, so running them in parallel
  (as CI always does) meant they all raced on writing lit's own `.lit_test_times.txt` cache
  file with no locking -- and once one run corrupted it, every subsequent lit invocation
  failed immediately, before running a single test, until the file was deleted. The cache
  has no value in CI (the build tree is fresh every run); disabled outright.
- **One module-interface test leaked `.pmi` files into the checked-in source tree**, the same
  class of mistake already fixed twice before in this project's history. Fixed, and `*.pmi`
  is now gitignored outright as a backstop.

## [0.3.0] - 2026-08-26

A foundation release rather than a features release, aside from one
exception: real `-g` debug info. The rest is a large internal-quality
investment — a code generator that had grown into a single ~1,800-line
class was decomposed into focused pieces, a from-scratch adversarial
review swept the whole compiler for bugs the existing suite couldn't see,
and the test suite was restructured to mirror how Clang splits its own —
plus a handful of further correctness fixes found along the way. Every
fix here carries a regression test verified to fail against the code it
fixes; the debug-info work additionally carries live gdb/lldb verification
passes, not just IR-text checks, per this project's own experience that
the two can disagree.

### Added

- **`-g` compiles debug information.** DWARF debug info covering real
  breakpoints, source-line stepping, and variable inspection under
  gdb/lldb: locals, parameters, and captured variables inside nested
  procedures, including `var` parameters and closures resolved correctly
  across static-link frames, plus an LLDB pointer-summary formatter for
  plang binaries. Previously `-g` was accepted and cleanly rejected with
  a "not supported" diagnostic (0.2.0); this replaces that rejection with
  the real thing.

### Changed

- **The code generator's internals were decomposed.** `Codegen::Impl` had
  grown to roughly 1,800 lines and 260 methods, mixing state from four
  different lifetimes — whole-program, per-module, per-function, and
  per-schema-instantiation — into one object. It is now around thirty
  focused, single-responsibility classes (a symbol table, a debug-info
  builder, a schema layout engine, and so on), each scoped to one
  lifetime or concern instead of all of them at once. Done as many small,
  independently-verified steps rather than one rewrite, and
  behavior-preserving by design: the full suite stayed green after every
  step, and steps touching captured-variable or closure debug info got an
  additional live-debugger verification pass rather than relying on the
  test suite alone, given this exact code's history of casing and
  debug-location bugs. No user-visible change from this on its own.

- **The test suite mirrors Clang's own split.** Most of it moved from
  GoogleTest to LLVM's `lit`+`FileCheck` — black-box tests that drive the
  real `plang` binary rather than constructing compiler objects directly
  in-process — matching how `clang/test/` relates to `clang/unittests/`.
  A small, permanent set of GoogleTest unit tests remains for internals
  with no CLI-visible surface (message-catalog bucket counts, pointer
  identity, an X-macro loop over builtin coverage). No effect on the
  compiler itself.

### Fixed

- **Assigning one schema instance's field to another's could read far past
  the source's allocation, corrupting memory or crashing.** A schema
  component copy such as `q^.x := p^.x`, where `x`'s own extent varies
  with the enclosing schema's discriminant, sized its `memcpy` purely from
  the *destination*'s runtime discriminants and applied it to the source
  address with no check that the two sides' discriminants actually agree
  — unlike the whole-object copy path a few lines above it in the same
  function, which already guards this with a discriminant-match check.

- **Allocating a schema array with a sufficiently large discriminant could
  silently allocate far too little memory, so the very next in-bounds
  write overflowed the heap.** `new(p, n)`'s body-size computation —
  `count = hi - lo + 1`, `bytes = count * elemSz` — multiplied two 64-bit
  runtime values with no overflow check before using the result both as
  the allocation size and as the `memcpy` length for whole-schema
  assignment, so a sufficiently large discriminant wrapped the byte count
  around to a small number while an index well within the declared extent
  still passed its range check.

- **A procedure with its own by-value conformant-array parameter, whose
  body forward-declares a nested sibling procedure, leaked that
  parameter's heap copy instead of disposing it.** Found while giving the
  code generator's per-activation state a real RAII owner: the copy was
  saved-and-cleared at function entry but only restored on the normal
  exit path, never on the early return a forward declaration takes.
  Confirmed with AddressSanitizer/LeakSanitizer before and after.

- **A schema whose body resolves to a var-string could reach the runtime
  allocator with a negative byte count.** The one existing guard against
  this was gated on the schema having exactly one discriminant; a second,
  unrelated discriminant bypassed it, previously caught only by luck
  (`calloc` rejecting the resulting huge size) with a misleading
  "out of memory" message rather than the real cause.

- **`substr` with very large index and count arguments could segfault
  instead of reporting a range error.** Its past-the-end check computed
  `i + n - 1 > ls` directly; for large enough operands the addition itself
  signed-overflows and wraps to a value that passes the check, letting an
  out-of-range call fall through to a `memcpy` with a source pointer far
  outside the string. Rewritten algebraically (`i>ls || n>ls-i+1`) so
  every operand stays bounded by the string's own length before use.

- **`abs()` of the smallest representable integer returned a negative
  number instead of a magnitude.** `minint`'s true magnitude has no
  positive representation in the same width, so negating it is
  signed-integer-overflow undefined behavior — in practice, a second,
  still-negative wrong answer. Now traps, matching the convention this
  runtime's own `plang_ipow` already uses for its undefined cases.

- **A source file with deeply nested parenthesized expressions crashed the
  compiler instead of producing a diagnostic.** Expression parsing
  recurses with no depth limit; around 20,000 levels of nesting reliably
  overran the real call stack. A depth counter now reports "expression
  too deeply nested" and unwinds cleanly well before that.

- **An out-of-range nondecimal integer literal silently wrapped to a
  meaningless value instead of being rejected.** Only the decimal literal
  path was checked for overflow; the `base#digits` path (§6.1.7)
  accumulated its value with no bound of its own and had already wrapped
  by the time the parser's overflow check ran.

- **A `write` call with a large runtime-computed field width could
  produce hundreds of megabytes of unintended output instead of a clean
  error.** The field width reached `printf`'s `%*d`/`%*c` through a bare
  cast to 32 bits with no range check first, so a width beyond what 32
  bits can hold silently reinterpreted as whatever the truncation landed
  on.

- **Two independent schema-layout bugs, found by finally enabling the
  cross-check meant to catch them.** Discriminant bindings were sorted
  alphabetically by name while nested schema bodies index them
  positionally by declaration order, so a multi-discriminant schema whose
  alphabetical and declaration orders disagreed bound every runtime
  discriminant to the wrong value; and a program that only ever writes
  concrete schema instantiations, never a bare reference, left a nested
  field's own discriminant resolved against a stale instantiation.

- **`new()` accepted a schema discriminant argument of the wrong ordinal
  type and silently truncated it instead of rejecting it at compile
  time.** Checked only for *some* ordinal type, never against that
  specific discriminant's own declared type the way an ordinary
  assignment already is.

- **A schema discriminant typed with a user-declared enum, named
  subrange, or type alias was always wrongly rejected as an illegal
  forward reference**, even when declared textually before the schema
  using it — only built-in keyword types worked. Discriminant type
  resolution ran in a Sema pass that always preceded the pass resolving
  ordinary type declarations' real bodies, regardless of source order.

- **Inside a `with` statement, a schema discriminant of any type other
  than plain `integer` failed type-checking against its own declared
  type**, though identical code written outside the `with` already
  worked correctly.

- **An identifier inside a `value` clause could resolve against the wrong
  scope.** It resolved against whatever scope codegen currently happened
  to be lowering rather than the scope the clause was textually written
  in, silently producing the wrong value wherever the two disagreed.

- **A `read`/`readln` call wrongly refused a protected `var` file
  parameter as its first argument.** A missing pair of braces made a
  protectedness check run over every argument including the file itself,
  though `read(f, v)` assigns to `v`, not `f`.

- **Under a debugger, a nested procedure's own local variable or
  parameter could show the captured outer variable's value instead of
  its own, whenever the two shared a name.** The compiled program itself
  was always correct; debug-info generation registered the captured
  variable's location under the nested procedure's scope before its own
  locals were bound, with nothing to disambiguate the two once both
  existed in that same scope.

- **Two "extra" input files sharing a filename in different directories
  silently clobbered each other's object file during multi-file
  compilation**, each defaulting to the same basename-only `.o` name with
  its directory discarded — the second compile overwrote the first's
  object file, and the linker read the same file twice.

- **An "extra" input file in multi-file compilation could not import a
  module declared by another extra file in a different directory**, even
  though both were given on the same command line and the first had
  already compiled successfully — each extra file's module search path
  was built from only its own directory.

- **A syntactically broken or wrong-module `.pmi` file was indistinguishable
  from a missing one, and could silently shadow a working `.pmi` later on
  the search path.** Module search stopped at the first candidate file
  that existed, not the first that actually loaded.

- **A 65th distinct bindable file variable silently lost its binding, with
  no error at all.** The binding table's fixed-size overflow case was a
  comment saying to ignore it; it now reports a real diagnostic.

## [0.2.1] - 2026-08-18

A second correctness sweep of 0.2.0's Extended Pascal support, this time for
conformance defects rather than missing features. Eleven fixes: four are
wrong output or a wrongly-accepted/wrongly-rejected program the standard
settles without ambiguity, and the rest fall under EP §6.10.3.1's "error"
class — violations a processor is explicitly permitted to leave undetected —
closed anyway, because plang already closes every comparable one elsewhere in
the language and two of them (the write-field-width family, `TimeStamp`'s
fields) are things a real program could plausibly hit by accident rather than
by writing deliberately nonconformant code. Where the standard's own
resolution was ambiguous or silent, the choice was checked against a real
Pascal implementation (FPC, in both its default and Turbo-compatibility
modes) rather than invented locally. Every fix carries a regression test
verified to fail against the code it fixes.

### Fixed

- **A bare nested structured-value-constructor failed to parse inside a
  named outer one.** EP §6.8.7.1's component-value grammar —
  `expression | array-value | record-value` — writes the two structured
  forms without a type name; the `value` clause's own constructor parser
  already knew this, but the one reached for a *named* constructor written
  as an ordinary expression (`TypeName[...]`) parsed every arm's value as a
  plain expression instead, which cannot start a structured form at all: a
  leading `[` is always a set-constructor to an expression parser, whose
  grammar has no colon anywhere in it.

  ```pascal
  type Inner = array[1..2] of integer;
       Outer = record a: Inner; b: integer end;
  var o: Outer;
  begin o := Outer[a: [1: 10; 2: 20]; b: 100] { was: expected ']', got ':' }
  ```

- **The semicolon before a case-statement's `otherwise` was wrongly
  required.** EP §6.9.3.5's grammar makes it optional — mandatory only
  between two ordinary case-list-elements — but the parser's arm loop
  treated any non-`;` token the same way, breaking out to an unconditional
  `expected 'end'` regardless of what actually followed. `case x of 1: foo
  otherwise bar end` was rejected.

- **A leading, trailing, or doubled underscore in an Extended Pascal
  identifier was accepted.** §6.1.3's grammar — `identifier = letter
  { [ underscore ] ( letter | digit ) }` — interleaves each optional
  underscore with a mandatory following letter-or-digit, and its own NOTE
  says the consequence directly: "An identifier cannot begin or end with an
  underscore, nor can two underscores be adjacent." The scanner tracked only
  whether an underscore occurred *anywhere* in a name, so `_foo`, `foo_` and
  `foo__bar` all passed silently under `-std=iso10206`.

- **A subrange bound could not call a required function.** `type t =
  1..abs(-5);` was rejected as "not a constant expression", though EP
  §6.8.2 makes a call to a required function (other than `eof`/`eoln`) with
  nonvarying arguments itself nonvarying — exactly what the standard's own
  constant-definition examples rely on (`pi = 4 * arctan(1);`, §6.3.2). The
  one general constant-folder plang reuses for every bound, `const`
  declaration, case-label and schema discriminant had no case for a call at
  all; it now folds `abs`, `sqr`, `succ`, `pred`, `ord`, `chr` and `odd`.

- **`bind` on an already-bound file silently rebound it.** §6.7.5.6 makes
  this a dynamic-violation outright — the standard's stronger class, which
  must be detected — not the weaker "error" a processor may leave
  unreported. The runtime's binding table was cleared and replaced
  unconditionally on every `bind` call with no check of prior state; it now
  reports the violation instead.

- **`date`/`time` printed a fixed placeholder instead of a `TimeStamp`'s
  real fields.** §6.7.6.9 specifies both purely as functions of the
  date/time fields; `DateValid`/`TimeValid` belong to `GetTimeStamp`'s
  contract, not theirs. A caller-constructed `TimeStamp` with `DateValid`
  (or `TimeValid`) false had its real `year`/`month`/`day` (or
  `hour`/`minute`/`second`) discarded in favor of a hardcoded
  `"0000-00-00"`/`"00:00:00"`.

- **`TimeStamp`'s `month`, `day`, `hour`, `minute` and `second` were
  unrestricted integers.** §6.4.3.4's own field-type declaration gives them
  subrange bounds — 1..12, 1..31, 0..23, 0..59, 0..59 — so `t.month := 13`
  compiled and ran with no diagnostic, unlike an out-of-range assignment to
  every other subrange-typed variable in the language.

- **Assigning an empty string to an adjacent-inverted substring silently did
  nothing.** §6.5.6 makes it uniformly an error for a substring-variable's
  first index to exceed its second — `s[j+1..j] := 'x'` (non-empty) already
  reported it, but `s[j+1..j] := ''` reached the one length check by
  coincidence (0 characters wanted, 0 assigned) and passed.

- **`type of x` did not count as using `x`**, so a variable named only to
  give another one its type via `type of` was flagged "declared but never
  used" — the same gap a for-loop's control variable already has its own
  explicit exception for.

- **A `value` clause did not count as an assignment**, so `var x: integer
  value 5;` (and the equivalent `type t = integer value 5; var x: t;`)
  still warned that `x` was "read here before it has been given a value" on
  its first use. The definite-assignment walk's initial state never
  accounted for either spelling of the clause.

- **A negative `write` field width was handled inconsistently, and worse
  than an error.** ISO §6.10.3.1 calls a negative `TotalWidth`/`FracDigits`
  "an error" without saying what a processor that acts on it anyway should
  do; checked against FPC, the answer a real Pascal implementation gives is:
  treat it as though no width had been written at all. plang's own handling
  varied by type and was worse in every case — a string or Boolean's
  *entire text* was silently dropped, an integer or char was left-justified
  by accident of a negative width also being a `printf` flag, and a
  negative `FracDigits` was quietly ignored rather than falling back to the
  exponential form. All of it now normalizes to "as if unspecified,"
  matching FPC.

## [0.2.0] - 2026-08-17

### Added

- **Undiscriminated schema types are implemented** (EP §6.4.4, §6.4.7).  A
  schema is a family of types indexed by a tuple of discriminants.  Where they
  are written out, `vec(4)`, the type is ordinary and always lowered like one.
  Where they are not — a pointer `^vec`, a formal parameter `v: vec` — the
  discriminants are only known at run time, and until now plang diagnosed the
  declaration rather than compiling it.

  They now travel with the value.  A schema formal parameter takes one extra
  i64 argument per discriminant, the same shape as the conformant-array bounds
  in §6.7.3.7; `new(p, d1..ds)` writes them into a header in front of the body,
  so `p^` can recover them anywhere the pointer reaches.  Everything that needs
  an extent — allocation, field offsets, array strides, index checks, string
  capacities — re-emits the body's own bound expressions against those values.

  The body may be a string, an array, or a record, including one with a variant
  part, nested records, arrays of records, and components whose own extent a
  discriminant fixes.  `with p^ do` opens it, `q^` may be assigned to and from
  a discriminated instance of the same schema, and a schema array may be passed
  to a conformant-array parameter, which receives its real bounds.

  `string` itself is one of these (§6.4.3.3), so `^string` and `new(q, 300)`
  work, and the capacity travels with the value rather than being guessed at.

  What is still refused, with a diagnostic that says so: a body that reads a
  discriminant without any extent, range or capacity of it saying so — there is
  nothing there to compute a layout from — and `pack`/`unpack` on an array
  whose element size a discriminant fixes.

### Changed

- **Code generation asks semantic analysis instead of working things out
  again.**  This is what 0.2.0 is for, and it is one cause rather than a
  collection: CodeGen decided what something *was* by looking up a NAME, by
  matching a SPELLING, or by reading a stale ANNOTATION, in places where Sema
  had already decided.  Its tables — `typeAliases`, `consts`, `schemaDefs_`,
  the scope stack used as a name oracle, and a constant folder of its own —
  have **no scope chain**, so they answer for whatever is innermost at the
  moment of lowering rather than where the declaration was written.

  Two shipped heap corruptions came from that, and the audit behind
  `docs/single-source-of-truth.md` found 35 sites.  The useful part of that
  document is not the count but the partition: which of the 35 are the same
  bug (a *foreign* denoter re-resolved here), which only look like it (a
  locally-written name, where the flat table is right), and which are not
  about names at all.

  What that came to, in the order it landed:

  - **Foreign nodes go through Sema.**  A denoter belonging to another scope is
    lowered from the type Sema resolved for it, not by re-reading its syntax.
  - **One constant folder.**  `ExprNode::ConstVal` carries the value Sema folded
    *in the scope the expression was written in*, and codegen's folder asks for
    it first.  Fabricated extents — a capacity of 255, a bound of 0 — are gone;
    a bound that does not fold is an error rather than a number.
  - **A schema extent is arithmetic, not an expression to re-run.**
    `ExtentForm` is closed arithmetic over the discriminants *by index*, with
    every other leaf folded where the declaration was written.  It contains no
    identifier, so nothing in the procedure doing the allocating can capture
    anything.  Every fallback that re-emitted a declaration's expression at a
    use site was measured at zero uses and then deleted, and the function that
    bound discriminant names for them no longer exists.
  - **One layout.**  A variant part's size and its field offsets are one walk;
    one function answers what its shared run must be aligned to; and the
    run-time layout walk is now compared against the static layout on **every
    record the compiler lays out**, not only on the schema programs somebody
    thought to write.
  - **A variable access is evaluated once.**  A string whose capacity a
    discriminant fixes needs an address and a capacity, and getting them
    resolved the access path twice — three times, measured — so a
    side-effecting subscript ran repeatedly.
  - **Widths come from types.**  How many bytes land in a typed file is a fact
    about the file's component type, not about whatever the expression happened
    to lower to.

  Programs compile to byte-identical IR across the ISO 7185 conformance corpus
  and the acceptance test, with one intended exception noted under Fixed.

### Fixed

- **A `packed record` no longer promises an alignment it cannot keep.**  plang
  packs `packed`, so fields sit at byte offsets that need not satisfy their own
  types' alignment — but the loads and stores were emitted with the alignment
  of the *value type*.  A `set of char` is `i256`, which this data layout aligns
  to 16, so a field at offset 1 produced `store i256 ..., align 16`: a promise
  about an address nothing had made true.  At `-O0` the backend used scalar
  moves and it ran; from `-O1` it emits `movaps` and the program dies.

  **The project's own acceptance program crashed this way at `-O2`**, after 935
  of its 1,211 lines.  The suite runs at `-O0`, and the acceptance test is only
  ever run at `-O0`.

- **A variant part containing a wide member was under-aligned.**  The blob its
  alternatives share capped its cell at `i64`, so a part holding a `set of char`
  was 8-aligned around a member needing 16.  This is the one intended IR change
  in this release: `iso7185pat.pas` now lays that record out as `[6 x i128]`
  where it was `[11 x i64]`, and nothing else moves.

- **`new()` allocated the wrong domain when the pointer's type was reached
  through a name that a procedure re-declared.**  Plain ISO 7185 — glibc aborts
  with "corrupted top size".  Sema's answer was already in that function, as a
  fallback reached only when the guess returned zero.

- **A type is identified by its declaration, not by its spelling.**  Two records
  sharing a name were assignment-compatible, so a three-field record could be
  copied into a one-field one — 24 bytes into 8, silently.  Same for two
  enumerations, which let an ordinal of 3 into a type whose largest is 2; and
  `packed` was ignored when comparing record shapes, so a padded record could be
  stored into a packed one.  Two new diagnostics say which question failed,
  since both types print identically.

- **A schema body binds its names where the body was written.**  It was resolved
  once per *instantiation*, in the scope the instantiation appeared in, so a
  local `const k = 1` beside `var h: v(2)` resized a body declared against a
  program-scope `k = 10`.  Likewise a local type of the same spelling changed
  what the body's components were.  And a discriminant now shadows a constant of
  its own name, where before `const n = 3` beside `type t(n: integer)` made
  every `t` three elements long whatever `new()` was told.

- **The `value` clause comes from the type that was written.**  Following a
  chain of type names re-bound every hop in the procedure being lowered, so an
  inner homonym supplied both the wrong initial value and its length — 400 bytes
  into a 4-byte allocation — and, in the other direction, dropped an
  initialization the type really has.

- **A schema whose body is another schema behaves like what it wraps.**  Looking
  through a schema is a loop, not a step, and was written as a step in a dozen
  places: `type v2(n) = vec(n)` could not be subscripted at all, and fixing only
  the check that refused it left a `1..4` array range-checked as `0..3`, so
  `x[4]` trapped and `x[0]` — outside the array — did not.

- **A field selected through a nested instantiation uses the instance's
  layout.**  `schemaPathOf` descended into a nested schema in its index arm and
  not its field arm, so `q^[i][j]` was right while `q^.x.k` was emitted at the
  probe's offset and landed on another field's bytes.

- **The same "loop, not a step" gap remained in six more places**, found by
  systematically trying the question above against every other one-hop
  `SchemaBody` peel in Sema and CodeGen: a `var x: B` formal for `B(n) = A(n)`
  had no fields at all (`schema 'B' has no discriminant 'id'`) though `var x:
  A` with the identical body worked; `q^` for a pointer to a schema-of-a-
  string-schema came back typed as the outer schema rather than the string, so
  `writeln(q^)` was refused as unwritable, and once that was fixed `new(q, 20)`
  sized the allocation from the probe's `string(1)` regardless; `with x do`
  bound none of a declared schema-of-a-schema's fields; and a schema-of-a-
  schema array was rejected by a conformant-array parameter that a directly-
  schema'd array already satisfied. A seventh instance of the same gap sat one
  level further out, in a conformant array's OWN element type: indexing past
  the conformant dimensions into a static element that was itself a schema
  instantiation used the wrong bounds and the wrong stride, landing a write on
  the neighbouring element instead — `a[lo][2]` writing where `a[lo][3]`
  belonged.

- **A structured-value constructor's untyped nested component took its shape
  from whatever the lowering procedure's own homonym type happened to be.**
  The type it should have taken its shape from is reached by recursing into a
  FOREIGN declaration — a record's field, an array's element — so the names in
  it belong to that declaration's scope, not to the procedure being lowered.
  `var r: rec value [f: [1:10; 2:20; 3:30]]`, where `rec.f`'s real type is an
  array declared elsewhere, aborted with `LLVM ERROR: plang codegen: array
  constructor has no array declaration...` whenever the lowering procedure
  happened to declare its own, unrelated type of the same name. Fixed the same
  way the sibling `writtenInitialState` bug was: by following
  `NamedTypeNode::Denotes`, which Sema records in the scope a name was
  actually written in, instead of the per-procedure spelling table. The same
  fix caught a second instance in `new(p)`'s own `value`-clause lookup, which
  had a size-agreement guard against exactly this but only for a SIZE
  mismatch — a same-size homonym slipped straight through it and applied the
  wrong initial value to a real allocation.

- **ISO §6.4.3.2's `packed array[1..n] of char` didn't get all the powers the
  standard gives it.** Assignment and comparison already treated it as a
  string; `length`, `substr`, `trim`, `index` and `+` concatenation did not.
  `length` fell to a raw `strlen` on the whole fixed-size array where a
  pointer was wanted — an LLVM IR verifier abort, not a diagnostic — `substr`/
  `trim`/`index` link-failed on a runtime symbol nothing ever emitted a
  definition for, and `+` silently truncated a char-array operand to its first
  character rather than rejecting or concatenating it. ISO 10206 §6.4.3.3.1
  is explicit that a fixed-string-type value "is a value of the
  canonical-string-type" and lists the concatenation operator among what a
  string-type is usable for, so all five now widen the same way assignment
  and comparison already did.

- **Crashes.**  A schema whose body names itself through a pointer — legal, and
  the ordinary way to write a linked structure — sent Sema into unbounded
  recursion and killed the compiler with no diagnostic.  Without the
  indirection the type really does contain itself, and that is now refused with
  a message naming the rule.  A schema whose body is a string is a string
  everywhere now, rather than an opaque aggregate an assignment stored a pointer
  into.

- **Extended Pascal that did not parse or did not check.**  A bare `string` as a
  **var** parameter now takes an actual of any capacity, which is the only way
  to write a procedure that modifies one; the capacity travels with the actual,
  so one body is bounded differently per call.  A typed set constructor with a
  single element or a single range is one (`cs['a']`), and a variable shadowing
  a type name is subscripted rather than read as a set.  An import-part is a
  list — `import A; B;` — as §6.11.3 writes it.  `for c in s` no longer reports
  its own control variable as never given a value.  And the probe value of 1
  that schema bodies are resolved under stopped reaching diagnostics, where it
  had been rejecting legal programs with bounds they never wrote.

- **`-g` compiles.**  It was forwarded to `llc`, which has no such option, so
  every `-g` build failed.  plang emits no debug information, and now says so
  rather than accepting the flag silently.

### How these were found, and what now guards them

The suite was green through every one of the above.  Its oracle is a printed
value, and a layout that is wrong but self-consistent prints the right one;
AddressSanitizer instruments the compiler and never the program it emits.

Four things exist because of that, and they belong before the fixes rather than
after:

- `test/tools/guardheap.c` butts every heap object against a guard page, so an
  over-run faults at the write instead of landing in a neighbour.  Several
  defects here printed correct output and exited 0 under a plain run.
- `test/tools/irgate.sh` compares emitted IR against a known-good commit, which
  compares what the compiler *did* rather than what one program observed.
- The `SchemaDifferential` harness compiles one body through every lowering and
  **crosses** writes against reads; two earlier versions did not cross, and real
  defects walked through both.
- Sema's layout, the static layout and the run-time walk are checked against
  each other on every record.

Each fix carries a test verified to fail without it.  Three tests written during
this work passed against the parent commit and tested nothing — a missing
declaration, a missing dialect flag — and were caught by that check rather than
by reading them.

- **A crash in the compiler no longer looks like a refusal to compile.**  llvm's
  `ExecuteAndWait` answers -1 for a failure to run and -2 for a child killed by
  a signal, and neither sets its `ExecFailed` flag.  The driver returned that
  straight through, so *any* internal crash exited 254 with no output at all.
  It is now reported and exits 1, like every other error.

- **A discriminant is no longer assignable.**  It is a value the schematic
  variable carries, not a component of it, but it is spelled like a field —
  so `q^.n := 5`, `v.n := 5`, `r(q^.n)` as a var parameter, and `n := 5` inside
  `with q^ do` were all accepted, and all four then killed code generation.
  Reading one is unaffected; that is how a program learns its own extent.

- **A string result no longer stops at 255 characters when its capacity is
  fixed by a discriminant.**  A temporary needs a size, and 255 is the answer
  for a capacity nobody knows — but here somebody knows it and simply does not
  know it yet.  Concatenation, `substr` and `trim` sized their result by that
  constant while telling the runtime the truth, so `q^ := q^ + 'x'` on a
  capacity of 300 quietly stopped at 256.  The temporary is now allocated to
  the same arithmetic, and released at the end of the statement so that one
  inside a loop costs a fixed amount of stack rather than one piece per pass.

- **Nested variant parts sat where only one of the two layout walks thought
  they did.**  The run-time size walk measured each alternative from zero and
  added the offset afterwards; the offset walk measured from the offset.  Those
  agree only when the offset is already aligned to the widest field in the
  part, which a nested run is not.  A field could sit four bytes from where an
  ordinary read of it looked, so a whole-value copy between `q^` and a
  discriminated instance silently changed it.  The two are now one walk.

- **`pack` and `unpack` checked the starting index against the probe's
  bounds**, which for `array[1..n]` and a four-element packed array came out as
  `1..-2` — a range describing nothing, refusing a legal program.

- Several defects in the same family, each fixed where the question was being
  answered twice: `p^` for a record body reading past the discriminant header,
  a nil check missing on one route to `p^`, an access path resolved twice per
  assignment so a side-effecting subscript ran twice, and an over-capacity
  string assignment that read past the end of a shorter allocation.

## [0.1.6] - 2026-08-14

A single fix, for a defect that corrupts the heap.

### Fixed

- **A schema body was sized in the scope where the object was allocated,
  not the scope where the schema was declared.**

  EP §6.4.7 lets a schema's extents be written as expressions:

  ```pascal
  const k = 3;
  type t(n: integer) = array[1..n+k] of integer;
  ```

  `new(q, 4)` re-emits those expressions to work out how much to allocate, and
  it did so at the point of the call.  Every identifier in them other than the
  discriminants was therefore resolved against whatever happened to be in
  scope *there* — so a local variable of an unrelated procedure captured the
  `k` the body was written against, and the object was sized from a run-time
  variable:

  ```pascal
  procedure alloc;
  var k: integer;              { nothing to do with the type }
  begin k := 1; new(q, 4) end; { allocates for k = 1, not k = 3 }
  ```

  The allocation comes out too small and the subsequent writes run off the end
  of it.  In the reported case the program aborted inside glibc with a
  corrupted heap; renaming that local to anything else made it correct.  The
  program's behaviour depended on the spelling of a variable in a procedure
  that has nothing to do with the type.

  The bounds are now emitted with only the schema's own discriminants and the
  compile-time constants visible, which is exactly the set of names the
  declaration could legally have used.

  Affects any program where a name used in a schema body's bounds is also
  declared as a variable somewhere an object of that schema is allocated.
  Present since schema types were introduced.

---

## [0.1.5] - 2026-08-12

**Where 0.1.4 went.**  It was cut from the 0.1.3 lineage rather than from this
one, so that its eight code-generation fixes could ship without the `packed`
record-layout change below.  This release contains all eight, and their entries
are folded into the list below rather than left in a section of their own —
they are the same class of defect as the rest of it, and separating them by
which release first carried them would only hide that.

### Added

- **Diagnostics can be translated.**  The English is still written in the four
  `Diagnostic*Kinds.def` catalogs and still compiled in; a translation of it
  is a GNU gettext `.po` file, read when plang starts.  `-fdiagnostics-language=`
  chooses one, and without it `LC_ALL`, `LC_MESSAGES` and `LANG` are consulted
  in that order.

  Entries are keyed by the diagnostic's identifier rather than by its English
  text, so rewording a message does not silently untranslate it in every
  language at once.  A translation may reorder the `%0..%9` arguments, which is
  what those have always been for, but one that drops or invents a placeholder
  is refused: `formatDiagMsg` substitutes nothing for an argument it has not
  got, and the result would be a sentence with a hole in it and nothing to say
  so.

  libintl is not linked.  The .po *format* is what buys a translator Poedit,
  Weblate and `msgmerge`; the library would buy a dependency macOS does not
  ship in `libSystem`.

  Everything that can go wrong ends in English: no catalog, an unreadable one,
  one from a newer plang, one with a malformed entry, one still half-written,
  and any entry marked `#, fuzzy`.  A malformed entry costs that entry and not
  the other 192.

  Because all of that is silent, `--version` now reports which catalog it
  resolved, and both CI install checks assert it — a catalog built but
  installed out of reach would otherwise leave a compiler that works perfectly
  and is never translated.

  There was a hook for this before, and it did not work.  `en_US.cpp` told a
  translator to copy it and "translate every string in the `Messages[]` table",
  but there are no strings in that file: it macro-expands the `.def`.  Anyone
  following the instructions had nothing to edit.  `-DPLANG_LOCALE`, which
  chose one such file at configure time, is gone; the language is chosen when
  plang runs.

- **The whole diagnostic line is translatable, not just the message.**  The
  severity label and the token descriptions are cataloged too, under
  `label/` and `token/`.  Without them a translated build would say
  "error: attendu identifier, obtenu end of file" -- the frame in one language
  and, first and last on the line, three words in another.

  A token with a fixed spelling is not offered to a translator at all.  `;`
  and `begin` are Pascal syntax and mean the same in every language.

- **Ten diagnostics that built a sentence out of two languages now say it
  themselves.**  `err_ep_extension` was "%0 is an Extended Pascal extension and
  is not available under -std=iso7185", with the subject arriving as an English
  noun phrase from each of ten call sites -- "an underscore in an identifier",
  "a range as a case-constant".  A language with grammatical gender or case
  needs the frame and the subject to agree, and English already needed a
  different article at each site.  It is eight complete diagnostics now.

  Where the inserted text is not prose -- an operator, a type name, an
  identifier -- it stays an argument, because Pascal syntax is not translated.
  The same split was made for the "lower"/"upper" bound diagnostics, the
  "packed"/"unpacked" arguments of `pack` and `unpack`, and the component type
  of a file.  None of this changes a word of the English output.

  The three places where a sentence is still assembled -- the list in
  `warn_case_not_exhaustive`, the "N or more" arity phrases, and the "(s)"
  plural dodge -- carry a TRANSLATORS note in the catalog saying so.  Fixing
  them properly costs more than it is worth for one message each.

- **Seven catalogs**, in `po/`.  `en_GB` and `en_CA` are in use: they are
  spelling deltas of three entries and one respectively, and they exist as much
  to exercise the machinery on something with no linguistic risk as for their
  own sake.  `fr`, `fr_CA`, `es` and `es_MX` are drafted but **marked fuzzy
  throughout**, so plang prints English for them until a native speaker clears
  each entry; `-fdiagnostics-show-fuzzy` reads the draft.

  A regional catalog is a delta laid over its language: `es_MX.po` is 31
  entries over `es.po`'s 214, and `es_ES` has no file at all and resolves to
  `es.po`.  The differences carried are the well-known ones —
  `fichero`/`archivo`, `matriz`/`arreglo`, `identifiant`/`identificateur`.

  The English catalog turns out to contain only two words that differ across
  en_US, en_GB and en_CA, and it is inconsistent with itself: it writes the
  American `labeled` and the British `unrecognized`.  The second cannot be
  corrected, because warning names are derived from their enumerator and the
  flag is `-Wno-unrecognized-argument`; a message spelling it the other way
  would send a reader to a flag that does not exist.  It keeps the British
  spelling in every English, with a note in the catalog saying why.

- **`-fdiagnostics-show-fuzzy`**, which uses translations a reviewer has not
  yet approved.  They are ignored by default, since an unreviewed guess at what
  a compiler error means is worse than English — but a catalog that is entirely
  unreviewed is then inert, and this is how the person reviewing it reads it.

### Fixed

- **Nineteen Extended Pascal names came back as "undefined function".**
  `cmplx`, `polar`, `re`, `im`, `arg`, the five direct-access procedures,
  `position`, `lastposition`, `empty`, `gettimestamp`, `date`, `time`, `bind`,
  `unbind` and `binding` were declared only inside `if (extendedPascal())`, so
  under `-std=iso7185` a program using one was told the name was undefined --
  which reads as a typo to go and fix rather than as a dialect boundary.  Ten
  other names, `card` and the string functions among them, were declared under
  either standard and said so properly.  The comment above the registration
  already described the second behavior as the intent; the split was not
  deliberate.  All of them now say what they are.

- **`read` into anything but a plain variable overran it.**  ISO §6.9.1's
  `read(v)` chose both the reader and the number of bytes to store from a
  lookup of the argument's *name*.  Only an identifier has one, so
  `read(a[i])`, `read(r.f)` and `read(p^)` fell through to a default of
  `i64` — which picked the *integer* reader for a `char` component and then
  stored eight bytes into one.

  ```pascal
  var s: array[1..4] of char;
  begin s[1] := 'Z'; s[2] := 'Z'; s[3] := 'Z'; s[4] := 'Z';
        read(s[1])                      { input: 5 }
  ```

  left `s` as `5` followed by three NULs and wrote four bytes past the end of
  the array.  A `file of 'a'..'z'` read into an element of an `array of char`
  did the same on the binary path, where the byte count came from the file's
  component type whether or not a temporary was used.

  Plain ISO 7185, no dialect flag, reachable from program input, and present
  since before 0.1.3.  Nothing in the suite caught it: the 377 conformance
  cases and the acceptance test read into named variables, and the IR of all
  181 modules they produce is byte-for-byte unchanged by the fix.  It was found
  while mapping the runtime boundary for Turbo Pascal, where a 16-bit `Integer`
  makes the same mismatch the normal case rather than the exception.

  The destination type now comes from Sema, which has one for every expression
  shape, rather than from a name that only identifiers have.

- **An array reached through two type aliases lost its lower bound.**  The
  bound was read with a single alias hop while the array's type was resolved
  through the whole chain, so `type row = array[5..10] of integer; rowalias =
  row;` left it at zero.  A legal `x[6]` aborted with "array index 6 out of
  bounds 0..5"; with `-fno-range-checks` the writes landed past the end of the
  array and overwrote the next variable.

- **A pointer lost its record when the type name was shadowed.**  `p^.field`
  resolved the domain type by name in a table rebuilt per procedure, so a
  nested procedure declaring its own type of that name re-aimed every `p^.f` in
  its body at the inner layout — with the field *index* still taken from the
  right record, so it read an unrelated offset.  `p^.b` gave
  1585267068834414592 for 22.

- **`with` bound variant fields to the wrong storage, or to nothing.**
  §6.4.3.3 lets a variant field be selected by name like any other, so the
  semantic field list is flattened while the record's storage holds one block
  shared by all the alternatives.  Pairing them by position bound the first
  variant field to that block — `with r do c := 4` stored an integer bit
  pattern into a `real` and printed 1.97626258336499e-323 — and never bound the
  later ones at all, so `with r do b := 22` referred to a symbol nothing
  defined and the link failed.  Both the record and the schema-body forms.

- **An integer actual did not widen for a `real` value parameter.**  §6.6.3.2
  makes a value parameter a variable the actual is *assigned* to, so §6.4.6
  applies.  Nothing coerced it, so `procedure scale(x: real)` called as
  `scale(3)` emitted a call that failed verification and the compiler died with
  an internal error.  A program could not use a `real` parameter without
  writing every actual as a real.

- **A substring took its capacity from whatever was at that address.**  The
  rvalue `s[i..j]` hunted for the source's capacity by scanning for a variable
  at the same address, defaulting to 255, so a substring of a field or an
  element was silently cut to 255 characters.  The scan was not sound when it
  matched either: a record whose first field is a string has the record's own
  address, so in `record s: string(20); t: array[1..5] of char end`,
  `th.s[1..10]` came back five characters long — its capacity taken from the
  field beside it.

- **A relayed conformant array lost every dimension but the first.**  §6.6.3.7.2
  permits passing a conformant parameter on to another conformant formal, and
  it is the ordinary way to factor code over one.  Only the outermost
  dimension's bounds were passed on; the rest came from a type that has no
  static bounds and arrived as 0..0, so the callee indexed the block with the
  wrong row width.

- **A second compilation in one process crashed.**  The file record was cached
  in a function-local `static`, so it outlived the `LLVMContext` that owned it
  and the next `Codegen` took a type belonging to a destroyed context — a
  segmentation fault in `llvm::Constant::getNullValue`, nowhere near the cache.
  The driver compiles once per process and never met it; the front end is a
  shared library precisely so that other things can read Pascal, and those
  compile repeatedly.  Found by the first tests to build two `Codegen` objects.

- **A field of a discriminated schema record could only be reached through the
  variable's own name.**  ISO 10206 §6.4.9 makes `small = buf(8)` an ordinary
  fixed-size type: an array component, a field of another record, a pointer
  domain.  Code generation resolved the struct behind a field access by asking
  whether the Sema type kind was `Record`, and a discriminated schema is kinded
  `SchemaInstance` with the record hung off `SchemaBody`, so the test failed and
  the access was rejected outright:

  ```pascal
  type buf(cap: integer) = record n: integer; s: string(cap); m: integer end;
       small = buf(4);
  var d: small; v: array[1..2] of small;
  begin d.n := 1;    { compiled }
        v[1].n := 1  { LLVM ERROR: cannot resolve the record type of field 'n' }
  ```

  `d.n` worked because a directly-declared variable takes an earlier path that
  reads the struct off the variable entry, which is what hid this: every other
  route — an array element, a field of another record, a function result — ICEd.
  The look-through was already written and already used by the two other
  consumers of the same expression; this one site did not call it.

- **A labelled statement could satisfy an enclosing block's label.**  §6.1.6
  gives a label-declaration-part the labels of the statements of *that* block.
  The placement check resolved the label with the ordinary enclosing-scope
  lookup, which answers by spelling, so a `1:` written inside a nested procedure
  was accepted against the program block's `label 1` — and the landing place was
  then planted in the declaring block's function, where nothing branches to it
  and the basic block ends without a terminator.  It surfaced as an LLVM
  verifier failure rather than as a diagnostic.

  The set of labels the current block declared was already kept, and the `goto`
  side already consulted it — that is precisely what makes a non-local goto
  recognisable as one.  Only the placement side never asked.  `goto` is
  untouched: naming an enclosing block's label is legal, and it is the
  statement that has to stay home, not the jump.

- **A field bound by a `with` was accepted as a `for` control variable.**
  §6.8.3.9 wants an entire-variable declared in the variable-declaration-part of
  the block containing the for-statement.  The check looked out past the
  with-statement's scope to find one — but returned the first name it found on
  the way, which inside a `with` is the field.  `with rr do for i := 1 to 3` then
  drove `rr.i`.  fpc `-Miso` calls it an illegal counter variable.

  The rule is about what the name *denotes*, not about whether some declaration
  of the spelling exists, so a field shadowing a variable that is declared in the
  block is refused too: inside the `with`, that name is the field.

- **A procedure could not assign the result of the function containing it.**
  §6.8.2.2 asks that the function block *contain* the assignment, not that it be
  it, so a procedure nested inside the function names the result as well:

  ```pascal
  function total(k: integer): integer;
  var n: integer;
    procedure setit;
    begin n := n * 2; total := n + k end;   { rejected: 'total' requires an argument list }
  begin n := k + 1; setit end;
  ```

  The check that accepts the assignment target knew this and searched the stack
  of functions whose blocks are open.  The check that gives the identifier its
  type asked only whether the innermost procedure happened to be that function,
  so the name fell through to the ordinary lookup and was read as a call with
  its arguments missing.  Both now ask the same question of the same place.

- **A nil schema pointer took the process down instead of raising.**  Indexing
  `p^` for an undiscriminated schema first reads the discriminants out of the
  header `new` wrote in front of the body.  That is a dereference of `p` as much
  as reaching the body is, and it was the one route to a `p^` that did not say
  so, so a nil `p` read the header at address zero and died of a segmentation
  fault.  A Pascal program could not report that, and `-fno-nil-checks` was not
  what turned it off.

- **`new` silently discarded arguments it had no reading for.**  §6.6.5.3 gives
  the extra arguments exactly two: variant case-constants for a record with a
  variant part, and — Extended Pascal §6.7.5.3 — discriminants for a schema.
  A domain type that was neither had them checked as expressions and then
  dropped, so `new(p, 8)` for a `^integer` allocated one integer and lost the 8
  without a word.

  The case that made this worth finding is `new(q, 20)` for a `^string`: it
  allocated a pointer's worth, and `q^ := 'a string'` then wrote a pointer into
  it and read back an empty string of length 1.  Extended Pascal §6.4.3.3 does
  make `string` a schema with a capacity discriminant, so that program is legal
  and plang does not implement it — it models the bare name as the unbounded
  string.  That one says so, and says to write `^string(20)` instead.


### Changed

- **How wide a type is travels with the type.**  `Type` carries `Width` and
  `IsSigned`, and `TypeContext::getInt(bits, signed)` interns integer types so
  that two `Word`s are one type.  ISO 7185 and Extended Pascal stamp 64 on
  everything and emit exactly the i64 they always did; Turbo has six integer
  types at four widths, and the width is what `SizeOf` answers, what a variable
  typecast's legality rule compares, and what a `file of T` image is made of.

- **One `SizeOf`, checked against the layout.**  `Sema::byteSizeOf` works sizes
  out without a DataLayout, because a Turbo `const BufSize = 4 *
  SizeOf(Integer)` has to fold before there is one.  Codegen asserts it against
  `DataLayout::getTypeAllocSize` for every type it lowers in every program it
  compiles, so a disagreement is a compile-time ICE rather than a `GetMem`
  buffer that is the wrong size.

  Writing the check found three things: Sema's field list is flat, so summing
  it counts storage a variant's alternatives share; a set aligns to sixteen,
  not eight, because it lowers to an i256 — which showed up not on a set but as
  eight missing bytes of tail padding in a record ending with one; and a schema
  instance has no size to give, its denoters carrying whichever instantiation
  was resolved last.

- **`packed` packs.**  ISO §6.4.3.1 leaves what it does to the implementation
  and plang used to do nothing with it.  A `packed record` is a packed struct
  now, in every dialect.  This moves the layout of packed records — one program
  in the acceptance test and three conformance cases use one — and changes the
  image a `file of packed record` writes.  Turbo needs real packing for
  `{$PACKRECORDS 1}`, and a `packed` that packs nothing is a word the language
  has that means nothing.

- **Compiler-switch state is positional.**  Turbo Pascal's `{$R+}` applies from
  where it is written to the end of the file, so one compilation can check one
  loop and not the next.  Nothing on `LangOptions` can say that, so the answer
  is a table of the places the state changed — `Basic/SwitchTable.h`,
  binary-searched by source location, with the switches themselves listed once
  in `Basic/CompilerSwitches.def`.  Range checking is the first consumer:
  `emitRangeCheck` asks where it is rather than reading a flag.

  A null table means the flag, and ISO 7185 and Extended Pascal have no
  directives to build one.  The 181 modules the conformance corpus and the
  acceptance test produce, across `-std=iso7185`, `-std=iso10206` and
  `-fno-range-checks`, are byte-identical either side of this.

  No `-fio-checks`/`-fno-io-checks` yet, though the roadmap called for one:
  ISO 7185 and Extended Pascal report an I/O failure by aborting, and
  `IOResult` — the whole reason to turn the check off — arrives with the Turbo
  file runtime.  The flag would have changed nothing and looked like it worked.
  `{$I}` is in the list and has a default for `{$I-}` to override.

- **A missing semantic type kind stops the build.**  `NumTypeKinds` counts the
  fourteen *type denoters* the parser produces, and four walks state the count
  they were written against so that adding one breaks the build at each of
  them.  What the denoters resolve to — `TypeKind` in `Sema/Type.h`, twenty-one
  kinds — had no such count, and the switches over it end in a `default:` and
  have to: most of them are asking a question only a few kinds answer.

  Four of those defaults are wrong rather than conservative, and now say so.  A
  scalar kind the definite-assignment walk has not been taught is silently not
  tracked, so using a variable of it before it is given a value stops being
  reported.  A structured kind the file check has not been taught lets a file
  be passed by value, against ISO §6.6.3.3.  A kind codegen has not been taught
  is reported as not lowerable, for no stated reason.  An ordinal kind the set
  check has not been taught skips the width check, which is the silent
  mask-truncation that check exists to prevent.

  The two walks in `Basic/SemaUtil.h` are counted now too.  They are `dyn_cast`
  chains rather than switches, so a kind left out is not a missing case but a
  subtree the walk never enters — and the §6.8.3.9 for-loop analysis and the
  definite-assignment walk both ride on them, so an omission there is a class
  of error that stops being reported rather than a line of output that goes
  missing.  Both are complete as written; that is what the counts pin.

- **The end-to-end suite is four binaries and a shared harness.**
  `driver_test.cpp` had reached eleven thousand lines and 742 cases, compiled
  as one translation unit however many cores `ctest` was given, and held the
  harness the next suite would need inside it — leaving that suite a choice
  between adding to the file and copying out of it.  It is now
  `DriverHarness.h` and four files split by what a case is about: the driver
  and the command line, what the generated code does when it runs, Extended
  Pascal, and modules.  Every one of the 742 cases came across; the test-name
  set was compared before and after.

  A case that needs several *named* files now gets a `CaseDir`: a directory of
  its own, removed with the case, and the working directory the program runs
  in.  Programs ran in whatever directory `ctest` was started from before, so
  a case that wrote `r.txt` wrote it into the build tree and left it there —
  three such files were sitting in `build/test/Driver` when this was written.

  `kEP11`, `kEP12` and `kEP13` were three names for `-std=iso10206`, each
  introduced beside a tier and used well away from it.  Splitting the file is
  what surfaced that; there is one now.

- **The required procedures and functions are one list.**
  `Basic/Builtins.def` holds each name once, with the dialects that require it,
  its arity and its result type.  Registration in `Sema.cpp` and the arity
  table in `SemaExpr.cpp` are generated from it, and a call carries the
  `BuiltinID` Sema resolved rather than a bare "was a builtin" flag, so what
  the front end decided is what the back end acts on instead of matching the
  spelling a second time.

  The three lists could not see each other, which is how nineteen names ended
  up declared one way and ten another.  Turbo Pascal adds about eighty more,
  which is what made one list worth having before they arrive.

- **Dialect gating asks what a dialect can do, not which one it is.**  A
  capability more than one dialect has is now named in
  `Basic/LangFeatures.def` and asked for as `Opts.has(Feature::X)`; the eight
  are declaration order, constant expressions in `const`, `case` ranges, the
  `case` default arm, subrange bound expressions, empty string literals,
  underscores in identifiers, and char concatenation.  The twenty-five sites
  that mean *Extended Pascal specifically* still say `extendedPascal()`, which
  is what they mean.

  Only shared capabilities are listed, deliberately.  Writing Extended Pascal's
  thirty extensions out as a matrix would mean transcribing a column by hand,
  and a wrong cell is invisible: both conformance corpora are ISO 7185
  programs, so neither an ISO 7185 mode that gained an extension nor an
  Extended Pascal mode that lost one would fail any of the 377.  A new
  `DialectGating` suite covers that direction with a pair per capability,
  asserting the specific diagnostic rather than mere rejection -- written the
  loose way, one of them passed with its gate deliberately disabled.

- **Code generation can see the dialect.**  `Codegen` took three scalars out of
  `LangOptions` and kept no record of which language it was compiling.  That
  held while every dialect difference was settled in the front end -- of the
  thirty-four sites that ask, none were in CodeGen -- but it is not where Turbo
  Pascal's differences live.

- **The dialect list is one list.**  `Basic/Dialects.def` generates the
  `Standard` enumeration and the validation both the driver and the front end
  perform.  They held four copies between them and had drifted: the front end
  listed the dialects in a different order and under a different word.

  It also fixes a latent bug.  The front end mapped every `-std=` that was not
  `iso10206` onto ISO 7185, so an unimplemented dialect would have compiled as
  standard Pascal without saying so -- harmless only for as long as the
  not-implemented check rejects it first.

- **The version is written in one place.**  It was written by hand in four —
  the shared library's `VERSION`, `Version.h`, the man page header and this
  file — and cutting 0.1.2 updated two of them, so that release shipped a
  compiler calling itself 0.1.2, a shared library built as 0.1.1, and a man
  page whose header agreed with the library rather than the compiler.  0.1.3
  brought them back together by hand, which fixes the symptom and not the
  reason.

  The root `CMakeLists.txt` now holds it, and `Version.h` and the man page are
  generated from templates.  Two of them cannot disagree because there is no
  longer a second one to disagree with.

- **A build between releases says so.**  It used to report the version of the
  release it came after, so a snapshot of the work leading to 0.2.0 called
  itself 0.1.3 and was indistinguishable from the release of that name.  It now
  says `0.2.0-pre`, and the suffix is emptied when the release is cut.  Semantic
  Versioning §9 orders a pre-release below the version it qualifies, which is
  the way round this wants: `0.1.3` < `0.2.0-pre` < `0.2.0`.

  Only what a human reads carries the suffix.  CMake rejects a pre-release in a
  project version outright, and a shared library called
  `libplang-frontend.so.0.2.0-pre` would be no better an idea for being
  accepted, so the soname stays numeric.

- **The message for an undiscriminated schema plang cannot lay out says whose
  limit it is.**  It read "schema 'buf' cannot be used without discriminants:
  its size varies with them and its body is not an array", which is wrong twice.
  Extended Pascal §6.4.4 and §6.7.3.7 both admit a bare schema-name where it
  fires, so the restriction is plang's and not the standard's; and the size does
  not always vary — `record k: 1..n end` is rejected too, and its storage is the
  same whatever `n` is.  What varies there is the range `k` is checked against.

  The restriction itself stands, and is not a narrowing: the body is resolved
  once against a probe binding of 1, and only an array body recovers, because
  the bound expressions are re-emitted against the discriminants at run time.
  Nothing else re-emits anything, so `string(cap)` would stay `string(1)` and
  those probe extents would become the actual offsets, sizes and range checks.
  Lifting it needs run-time field offsets, a run-time body size, per-field bound
  recovery, a string representation carrying its capacity, and a Sema that marks
  a discriminant-dependent extent unknown rather than folding it.  The comment
  at the check now records that, so the next reader does not have to rediscover
  which of the two it is.

### Why the suite did not catch any of this

The 377-case ISO 7185 conformance suite and the 3000-line acceptance test use
one type alias, no shadowing, no `with` over a variant part, named variables for
input, and no integer actual for a real formal.  Every one of the 181 LLVM
modules they produce is byte-identical before and after nearly every fix in this
release: the corpus cannot see this class of defect at all.  It is strong on
what the standards say and blind to what the compiler assumes about itself.

That is the finding of the release, more than any one of the thirty.  A test
suite that says what a language means will not tell you when the compiler has
quietly asked a different question — here, what a name refers to — and got a
different answer.  The defects were found by reading for the class rather than
by running anything, and three of the eight in 0.1.4 turned out to have
untouched twins that the same reading found only on a second pass.

Tests were added for the shapes the corpus does not reach, and each fix was
checked by reverting it and confirming that exactly its own tests fail — which
caught two cases where a test passed for a reason other than the fix.

## [0.1.3] - 2026-08-11

Four bug fixes, all in code generation, and none of them new: every one was
present in 0.1.0.  A program may now declare its own `abs` or `close` and have
its own called, a nil dereference is still reported when the bounds checks are
turned off, and the file record can no longer be changed on one side of the
compiler without the other.

The symbols a compiled program defines have been renamed, so this is not
binary compatible with 0.1.2: recompile, rather than relink, anything built
with `-c` under an earlier version.  Nothing about the language plang accepts
has changed.

### Changed

- **American spellings throughout.**  The source mixed the two: it wrote the
  American `labeled` and the British `unrecognised`, `colour`, `initialised`,
  `finalisation`, `tokenise` and a dozen more, in comments, message text,
  identifiers and test names alike.  All of it is American now, in one pass.

  One of those is user-visible.  A warning is named after its enumerator, so
  renaming `warn_unrecognised_argument` renames the flag: **`-Wno-unrecognised-argument`
  is now `-Wno-unrecognized-argument`**, and the old spelling is rejected as an
  unknown warning.  It was the one British spelling that could not simply be
  corrected in place, because before this the message and the flag had to agree
  and the flag was the British one; now they agree on the American.

  The language catalogs are deliberately untouched: `en_GB.po` and `en_CA.po`
  exist to spell things the other way, and `fr`, `es` and their regional deltas
  are not English at all.  Their `msgctxt` keys did have to follow the renamed
  identifiers, since a key names a diagnostic rather than a message, and a test
  asserts every shipped catalog resolves against the current source.

- **`Codegen`'s pointer to its implementation is called `PImpl` again.**  It was
  `PascalImpl`, which reads as though it were the Pascal half of something with
  another half somewhere.  `PImpl` is what the idiom is called, so it says what
  the member is to anyone who has met it before.  Naming only; nothing about
  what is generated has changed.

### Fixed

- **A program may declare its own `abs`.**  ISO §6.2.2.10 lets a program
  redeclare a required identifier, and the declaration then denotes what the
  program said and not the required procedure or function.  Codegen decided
  which was meant by lowercasing the name and running an if-chain over the
  required ones, before anything had asked which declaration was in scope where
  the call was written — so wherever the two were spelled alike, the required
  one won.  A program declaring `function abs(x: integer): integer` and calling
  `abs(-3)` printed 3: its own body was compiled, and nothing ever called it.

  Where the redeclaration took different arguments this was worse than a wrong
  answer.  A declared `procedure close(x: integer)` reached the required
  `close`, which takes a file, and was emitted as a call to it with no arguments
  at all — caught, if it was caught, by the LLVM verifier reporting a null
  operand, and reported as an internal error rather than as anything to do with
  the program.

  Which declaration a name denotes is a question about the scope the name was
  written in, and Sema is the only phase that knows.  It now records what it
  resolved, on `CallExpr` and `CallStmt`, and codegen consults that before
  reaching for the name: a call Sema did not resolve to a required routine is
  not one, whatever it is spelled.  Nothing about the required routines
  themselves has changed.

  This also settles a functional parameter named after a required function.
  `emitCallExpr` checked for one only after the whole required chain, the
  reverse of what `emitCallStmt` did, so a parameter called `abs` was never
  reached; the check that resolves this comes first for both now.

- **A program may declare its own `close`.**  Everything the source names was
  mangled into `plang_*`, and so are the runtime's own ~150 entry points, so the
  two halves of every link shared one namespace.  Thirty-three names collided,
  twenty-four of them required identifiers ISO §6.2.2.10 entitles a program to
  redeclare: `close`, `reset`, `rewrite`, `page`, `halt`, `round`, `trunc`,
  `sqrt`, `sin`, `ln` and the rest.  A program that declared one asked the
  linker for a symbol the runtime had already defined.

  What made it hard to see is that it did not always fail.  The runtime is a
  static archive, so the twin only reaches the link when the translation unit
  holding it is pulled in for some other reason — which is why a procedure
  called `time` linked and one called `close` did not, and why which programs
  broke depended on what else they happened to use.

  Procedures and functions are `pas_` now, and variables `pasg_`; nothing in the
  runtime begins with either, so no declaration can collide with it whatever it
  is called.  The mangling is otherwise unchanged, and the prefixes are named
  constants in one place rather than a literal at each of the nine sites that
  built one.

  This changes the object file ABI: a `.o` from an earlier plang does not link
  against one from this version.  Recompile, rather than relink, anything
  compiled with `-c` before.  `.pmi` files are unaffected — they are Pascal
  source, and name nothing mangled.

- **An underscore in a name is not a scope separator.**  An enclosing scope — a
  module, or the procedure a procedure is nested in — was joined to what it
  declares with `__`, which Extended Pascal §6.1.3 allows inside an identifier,
  so a mangled name did not separate into its parts one way.  A module `a`
  exporting `b` and a top-level `a__b` were both `pas_a__b`: LLVM renamed the
  second definition, every call reached the first, and nothing was reported.  A
  program with both printed the same answer twice.

  They are joined with `$` now, which is not in the Pascal alphabet, so no
  identifier can be mistaken for a scope boundary and none can forge one.  It
  is accepted unquoted in an LLVM identifier and in an ELF and a Mach-O symbol;
  `-S` output still assembles with the system assembler.

- **`-fno-range-checks` no longer removes the nil-dereference check.**  ISO
  §6.5.4 makes dereferencing `nil` an error, and plang reports it rather than
  leaving it to the hardware, which would answer with a signal and no
  indication of which line.  That check had been grouped with the array-index
  and subrange checks and so was turned off with them.  The two are not the
  same request: asking for indexing not to be checked is a statement about what
  a bounds test costs inside a loop, and says nothing about wanting a nil
  dereference to become a segmentation fault.  It has its own flag now,
  `-f{,no-}nil-checks`, on by default.

- **The file record is declared once.**  A Pascal file variable is a
  `PascalFile`, and the runtime and codegen each said what that was: the C++
  struct in `plang_file.cpp`, and the equivalent LLVM `StructType` in
  `fileStructType()`.  Holding them together was a `sizeof` assert on the
  runtime side, which could not see the codegen encoding at all — so a field
  added, widened or reordered on one side alone gave generated code a field at
  an offset nothing had written it to, with nothing reported anywhere, since
  the sizes still agreed.  The struct moves to
  `include/plang/Basic/PascalFileLayout.h`, which both sides read, and codegen
  now checks the type it builds against that struct field by field and as a
  whole.  This follows `plang/Basic/Arith.h`, which the runtime and the
  constant folders already share for the same reason.

  Nothing about the layout itself has changed.  The stale comment calling it
  16 bytes is gone.

## [0.1.2] -- 2026-08-10

### Added

- **plang targets macOS.**  It already built there; it could not produce an
  executable, because the driver had one link recipe and that recipe was ELF's:
  `ld.lld`, an ELF emulation, an ELF interpreter, and the startup files and
  `libgcc` of a GCC installation, none of which a Mac has.  Everything up to
  the link was already portable — `llc` writes a Mach-O object on a Mac without
  being asked to — so the link is what this adds.

  The driver now picks a linker from the target triple.  ELF targets are linked
  exactly as before.  Darwin targets are linked with the system `ld` against the
  macOS SDK, which is a much shorter recipe than the ELF one: macOS has no
  startup files to find, and `libSystem` is the C library, the maths library and
  the threads library at once, so the system side of the link is `-lSystem`
  inside the SDK.  What is left is finding the SDK, which is `SDKROOT` if it is
  set and what `xcrun` reports otherwise; and finding the compiler builtins,
  because the complex multiply and divide the runtime needs are calls the
  compiler emits rather than code, and they arrive with `-lgcc` on Linux and
  from `libclang_rt.osx.a` here.  `ld` is located through `xcrun` rather than
  taken off `PATH`, so that a GNU `ld` installed alongside — Homebrew's binutils
  puts one there — is not handed a Mach-O link.

  The deployment target is settled in one place and told to both halves.  It is
  `MACOSX_DEPLOYMENT_TARGET` if that is set and comes from the target triple
  otherwise, and it reaches the object file through the triple given to `llc`
  and the executable through `-platform_version` given to `ld`.  Deriving it
  once is what keeps them equal: `ld` warns about every object file whose
  deployment target differs from the one it is linking for, and the front end's
  triple carries a Darwin kernel version, which is not a macOS version and says
  nothing about which macOS a program is meant to run on.

  The runtime library is built for the oldest macOS either architecture runs,
  rather than for the machine it was built on, so that it can be linked into a
  program built for any of them.  Built for the host it would still work, but
  `ld` warns about each object in an archive that was built for a newer macOS
  than the link is for, and anyone who set `MACOSX_DEPLOYMENT_TARGET` to
  anything but their own version would get a page of them on every link.

  Two things outside the driver were also Linux-shaped.  The installed `plang`
  found its front-end library through an rpath of `$ORIGIN/../lib`, which dyld
  does not understand; on macOS it is `@loader_path/../lib`.  And the one test
  that has to stop a program that might wait forever called `timeout`, which is
  GNU coreutils and is not on a Mac, so it now starts its own watchdog and
  waits for whichever finishes first.

  CI runs the whole suite on macOS, in Debug and Release. That job replaces the
  `libc++ (compile only)` one, which existed to approximate a macOS build from
  Linux and could not link; the real thing compiles, links, runs the tests and
  checks the install rules, and it is also the only job on AArch64.

### Fixed

- **A non-local `goto` no longer resets the signal mask.**  The landing pad
  was entered with `_setjmp` and jumped to with `longjmp`, which are not a
  pair: the two forms of each differ in whether they carry the signal mask,
  and mixing them is undefined.  It happened to be harmless with glibc, which
  records in the buffer whether a mask was saved and has `longjmp` restore one
  only if it was.  macOS does not record anything of the sort — `longjmp`
  restores a mask from the buffer whatever put it there — so every non-local
  goto set the signal mask from whatever the buffer happened to hold.  For the
  program-level buffer, which is zeroed, that unblocked every signal the
  program had blocked; for a procedure's, which is a stack slot, it was
  whatever was on the stack.  The jump is `_longjmp` now, which is the partner
  of the `_setjmp` that was already there, and is in glibc and Apple's libc
  alike.

- **plang builds against libc++.**  It had only ever been built against
  libstdc++, and leaned on two things libc++ does not provide, so the build
  failed on macOS, where clang uses libc++ by default.  `AstPrinter` was
  written in terms of `std::views::enumerate`, which is C++23 and which libc++
  has not implemented, in ten loops; and `SemaFlow` used `std::inserter`
  without including `<iterator>`, which libstdc++ happens to supply through
  another header and libc++ does not.  The second was a plain missing include
  and always a bug.

  The ten loops all wanted an index for one purpose, to write a space before
  every item but the first, so they say that instead and no longer need an
  index at all.  `std::views::zip` and `std::print`, the other C++23 library
  features in the source, are both in libc++ and are left alone.

  A `libc++ (compile only)` job built every translation unit that way, stopping
  short of linking because the `libLLVM` from apt.llvm.org is built against
  libstdc++ and the two disagree about `std::string`.  The macOS job above has
  since taken its place and it has been removed.

  This made plang **build** on macOS rather than target it; the entry above is
  the rest of that work.

## [0.1.1] - 2026-08-10

A build fix.  Nothing about the language plang accepts, or the code it
generates, has changed since 0.1.0.

### Fixed

- **plang builds with GCC again.**  Four recursive walks — the label-nesting
  pre-scan in `Sema`, the variant-part walks in `SemaType` and `CodeGen`, and the
  non-local goto scan in `CodeGen` — were lambdas taking an explicit object
  parameter, `[&](this auto& Self, ...)`, so that they could call themselves.
  Three of them named a member of the enclosing class in the body, and no
  released GCC compiles that: 14.3 rejects it as an `invalid use of non-static
  data member`, and 15.2 crashes with an internal compiler error in
  `finish_non_static_data_member`.  Only GCC 16 and clang accept it, which made
  the README's "GCC >= 14" untrue for every GCC anyone has.

  They are private recursive member functions now, which is what the rest of
  `Sema` and `Codegen::Impl` already are, and the recursion is an ordinary call
  rather than a capture.  Nothing about what they do has changed; the deducing-
  this form bought only that the lambda could be written where it was used.  CI
  builds under both GCC 14 and GCC 15 so that this cannot come back unnoticed.

## [0.1.0] - 2026-08-10

Initial release.  Full support for ISO 7185 Standard Pascal at Level 1 and
ISO 10206 Extended Pascal.  Support for other Pascal dialects and extensions
planned for future releases.

[0.1.5]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.5
[0.1.4]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.4
[0.1.3]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.3
[0.1.2]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.2
[0.1.1]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.1
[0.1.0]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.0

---

