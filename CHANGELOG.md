# Changelog

All notable changes to plang are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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

