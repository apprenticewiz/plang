# Changelog

All notable changes to plang are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

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

[0.1.2]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.2
[0.1.1]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.1
[0.1.0]: https://github.com/apprenticewiz/plang/releases/tag/v0.1.0

---

