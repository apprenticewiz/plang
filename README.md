# plang — Pascal LLVM Compiler

plang is a Pascal compiler built on LLVM.  It implements ISO 7185 Standard
Pascal at level 1 — conformant array parameters included — and ISO 10206
Extended Pascal, with future support planned for other Pascal dialects and
extensions, including Turbo Pascal, Delphi (Object Pascal), and Free Pascal.

## Background

plang is an experiment in crafting a Pascal compiler as a modern LLVM
toolchain, utilizing the substantial code optimization and generation
framework that LLVM offers.  plang is intended to be intuitive for users
who are accustomed to working with gcc and clang, and other compilers in
those families.

I will be fully honest and transparent: this compiler was built using the
assistance of AI technology.  I understand that some of you will have
deep philosophical issues with this, but it is not up for debate.
Another aspect of this experiment is to explore and learn what AI
technology can help us achieve as designers and developers.  If you have
an issue with this stance, then there are plenty of other Pascal
solutions available for you to use.  I wish you nothing but the best.

That said, I do strongly believe in code quality.  The bottom line is
that this compiler needs to be something that everyone can proudly and
confidently use.  AI can make mistakes, and merely putting out AI slop is
not useful to anyone.  Checking what AI produces is essential to ensure
the quality of the compiler.  One of the aims of this project is to
produce new tests and sourcing existing ones to help validate the
quality of this compiler and improve it wherever we can.

This project is not intended to take away from the excellent work of the
Free Pascal project.  I greatly admire their efforts to promote and
support the Pascal langauge.  I simply offer plang as another alternative
solution for users who want to compile Pascal programs and leverage the
use of the LLVM technology stack.

I welcome issue reports and contributions from the community.  If you
happen to find an issue, please report it on the issues page for this
project.  I am also very open to pull requests for quality fixes and
enhancements.

## Status

plang should be considered **alpha** quality at this time.  I am actively
iterating through issues and new features, but I would not consider it
ready for production just yet.  There are likely bugs yet to be
discovered.

plang intends to support a rich set of Pascal language dialects:

| Dialect (`-std=`) | Description | Status |
|-------------------|-------------|--------|
| `iso7185` | ISO 7185 Standard Pascal, level 1 | Feature complete |
| `iso10206` | ISO 10206 Extended Pascal | Feature complete |
| `turbo` | Turbo Pascal extensions | Planned |
| `delphi` | Delphi / Object Pascal extensions | Planned |
| `fpc` | Free Pascal extensions | Planned |

Unimplemented dialects are accepted on the command line but rejected
with an error; only `iso7185` and `iso10206` are currently accepted.
`iso7185` is the default Pascal dialect unless another dialect is
specified.

plang currently passes all of the tests for ISO 7185 Standard Pascal in
the [Pascal Acceptance Test](http://pascal.hansotten.com/standard-pascal-and-validation/pat/).
As no comparable suite exists for Pascal extensions such as ISO 10206
Extended Pascal and others, support for these extensions is considered
best effort.  I will do my best to find and fix any bugs as they are
encountered.

## Documentation

| Document | Contents |
|----------|----------|
| [`docs/technical_info.md`](docs/technical_info.md) | How the compiler is put together, its diagnostics and warnings, and the test suite |
| [`docs/conformance.md`](docs/conformance.md) | The ISO 7185 clause 5.1 documentation: implementation-defined and implementation-dependent behaviour, and which of the standard's errors are reported |
| [`docs/modules.md`](docs/modules.md) | Extended Pascal modules and separate compilation |


## Prerequisites

- CMake ≥ 3.16
- LLVM (any recent release; tested with 22.x)
- GCC ≥ 14 or Clang ≥ 18 (C++23 required)
- lld (`ld.lld` must be on `PATH` for linking)
- Google Test — only to build the test suite, which is off by default

## Building and Installing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

The test suite is off by default and is built with `-DPLANG_ENABLE_TESTS=ON`.
See [`docs/technical_info.md`](docs/technical_info.md) for details as to
what is in it.

## Usage

```bash
# Compile and link
plang hello.pas -o hello

# Compile to object file only
plang -c hello.pas -o hello.o

# Emit LLVM IR
plang -emit-llvm hello.pas -o hello.ll

# Emit native assembly
plang -S hello.pas -o hello.s

# Optimization
plang -O2 program.pas -o program

# Dialect selection (iso7185 is the default; turbo, delphi and fpc are planned)
plang -std=iso10206 program.pas -o program   # Extended Pascal

# Show the commands without running them
plang -### program.pas -o program

# Warnings are all on by default; turn one off, or all of them
plang --help-warnings                        # list them
plang -Wno-unused-parameter program.pas
plang -Werror program.pas
plang -w program.pas

# Give up after a set number of errors rather than cascading
plang -ferror-limit=5 program.pas
```

## License

Apache License 2.0 with LLVM Exceptions.  See [LICENSE.md](LICENSE.md) for the full text.
