# Changelog

All notable changes to plang are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

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

### Changed

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

[0.1.3]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.3
[0.1.2]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.2
[0.1.1]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.1
[0.1.0]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.0

---

