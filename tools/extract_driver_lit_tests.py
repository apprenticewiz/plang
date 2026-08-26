#!/usr/bin/env python3
"""Extract test/Driver/{ep,module}_test.cpp's GoogleTest cases into
standalone .pas files under test/{EP,Module}/ (issue #34 Phase 3).
Originally written for codegen_test.cpp/test/CodeGen/ (Phase 2, PR #37);
generalized here to take a suite name (see SUITES below) since the two
layers this script is built from -- call-shape recognition (this file's
CALL_RE/extract_call_args/compile_lines_and_body) vs. assertion-shape
recognition (the BUILDERS list) -- turned out to need no changes at all in
the second layer to serve ep_test.cpp too; only the first layer's flags
handling grew a second case (see KEP_PLUS_RE below).

Unlike Phase 1's Conformance suite (machine-generated, perfectly uniform,
raw-string-delimited), these files are hand-written with genuine structural
variety, and use ordinary C++ string literals (auto-concatenated across
adjacent "..." tokens, \\n as the only escape in use -- confirmed by grep
before writing this) rather than raw strings. This script handles the
common, mechanically-convertible shapes and explicitly SKIPS (reports, does
not guess) anything with real control flow (for/while loops, more than one
compile call, a stdin-text third argument, a computed flags expression it
doesn't recognize) or an assertion shape it doesn't recognize -- those get
converted by hand.

Flags handling: compileAndRun/compileAndEmitIR's second argument (ExtraFlags)
is literal compiler flags text (kEP resolved to "-std=iso10206", `kEP +
"literal"` / `std::string(kEP) + "literal"` resolved the same way with the
literal suffix appended -- see KEP_PLUS_RE -- everything else used verbatim).
It is never silently dropped: an earlier draft mapped the dialect value
straight to a %plang_ep/%plang choice and dropped every non-dialect flag
(-fno-range-checks etc.) in the process -- found for real by grepping every
distinct second-argument value actually in use, not assumed from a smaller
sample. base_and_flags() below reintroduces a %plang_ep choice, but only as
a pure DRY win for the ep suite (stripping a literal, already-matched
leading "-std=iso10206" off the interpolated text) -- it falls back to
%plang with the flags text unchanged whenever that isn't safe, so nothing
is ever dropped.

Shapes handled:
  1. compileAndRun + exact EXPECT_EQ(R.Stdout, "...") + ExitCode==0
     -> RUN: %plang <flags> %s -o %t / RUN: %t | FileCheck --strict-whitespace
        --match-full-lines %s, with CHECK/CHECK-NEXT per output line.
  2. compileAndRun + EXPECT_NE(R.Stdout.find(...), npos)-style substring
     check(s) -- the original assertion was ALREADY substring-based, so
     plain CHECK-DAG (order-independent) is the faithful translation, not
     the exact-match idiom above.
  3. compileAndEmitIR + irContainsAll/irContainsNone
     -> RUN: %plang <flags> -emit-llvm %s -o %t.ll / RUN: FileCheck %s < %t.ll
        with CHECK-DAG (irContainsAll) / CHECK-NOT (irContainsNone) --
        CHECK-DAG, not sequential CHECK, because the C++ list's order is not
        guaranteed to be IR emission order (confirmed empirically elsewhere
        in this project's design work).
  4. compileAndRun + EXPECT_NE(R.ExitCode, 0) or EXPECT_EQ(R.ExitCode, N>0)
     + a checked R.Stderr substring (one or more)
     -> two different RUN shapes depending on whether compileAndRun's
        nonzero ExitCode came from the COMPILE step or the RUN step (these
        are genuinely different -- confirmed by reading DriverHarness.h's
        compileAndRunFile: a failed compile returns the compiler's own exit
        code with empty Stdout and never runs anything; a failed run returns
        the PROGRAM's exit code). Distinguished by whether any checked
        substring starts with "plang runtime:" -- this project's own
        established, consistent convention for every runtime-library
        diagnostic (confirmed throughout this session) -- vs. a Sema/Parser
        diagnostic, which never has that prefix. An exact pinned exit code
        (vs. just nonzero) is deliberately NOT preserved -- lit's internal
        shell has no $? expansion (confirmed empirically: 'cmd; test $? -eq
        N' does not work, it is not a real POSIX shell) -- the stderr
        message check already does most of that discriminating work.
  5. compileAndEmitIR + EXPECT_FALSE(R.Ok) (+ an R.Stderr check) -- a
     Sema/parse rejection caught on the -emit-llvm path.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PLANG_BIN = REPO_ROOT / "build" / "bin" / "plang"

# suite key -> (source .cpp, output test/ directory). codegen_test.cpp,
# ep_test.cpp, and module_test.cpp are gone (deleted in PRs #38/#39/#40).
# driver_test.cpp's own GTest suite is literally named "Driver" (19 cases),
# so those land at test/Driver/Driver/<kebab-name>.pas -- the category
# dir and the sub-suite dir both being "Driver" is a real, harmless artifact
# of the existing out_dir = out_root / suite convention, not a bug.
SUITES = {
    "driver": (REPO_ROOT / "test" / "Driver" / "driver_test.cpp", REPO_ROOT / "test" / "Driver"),
}


def compile_succeeds(src: str, flags: str | None) -> bool:
    """Ground truth for whether a nonzero R.ExitCode came from the COMPILE
    step or the RUN step: actually invoke the real, just-built plang binary
    rather than guessing from the checked message text. An earlier attempt
    heuristically checked whether the message started with "plang runtime:"
    (this project's own consistent prefix for every runtime diagnostic) --
    wrong in practice, because several tests check only a prefix-free
    fragment of the real message (e.g. "div by zero" for the real "plang
    runtime: div by zero"), found only by actually running the generated
    .pas files through real lit and reading the failures, not by reasoning
    about the C++ source alone."""
    with tempfile.TemporaryDirectory() as d:
        src_path = Path(d) / "case.pas"
        src_path.write_text(src)
        bin_path = Path(d) / "case.out"
        cmd = [str(PLANG_BIN)] + (flags.split() if flags else []) + [str(src_path), "-o", str(bin_path)]
        proc = subprocess.run(cmd, capture_output=True, timeout=30)
        return proc.returncode == 0

TEST_HEADER_RE = re.compile(r'TEST\((\w+),\s*(\w+)\)\s*\{')


def strip_line_comments(text: str) -> str:
    """Strip C++ // line comments, string-literal-aware (an inline comment
    after a flags argument, e.g. "-std=iso10206"  // why, otherwise gets
    swept into the argument text by the naive depth-scanner below -- found
    for real running this against the actual file, not anticipated up
    front). No // ever appears inside this file's Pascal source strings
    (Pascal has no // syntax), so this is safe without deeper C++ lexing."""
    out = []
    in_str = False
    i = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            in_str = not in_str
            out.append(c)
        elif not in_str and c == "/" and i + 1 < len(text) and text[i + 1] == "/":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def find_test_bodies(text: str):
    """Yield (suite, name, body_text_including_braces) for every TEST(),
    using brace-depth counting that ignores braces inside "..." string
    literals (confirmed no \\" escapes exist in this file, so a bare
    unescaped '"' always toggles string-literal state)."""
    for m in TEST_HEADER_RE.finditer(text):
        start = m.end() - 1  # index of the opening '{'
        depth = 0
        in_str = False
        j = start
        while j < len(text):
            c = text[j]
            if c == '"':
                in_str = not in_str
            elif not in_str:
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        break
            j += 1
        else:
            raise ValueError(f"unterminated TEST body for {m.group(1)}.{m.group(2)}")
        yield m.group(1), m.group(2), text[start : j + 1]


STRING_LIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

# The whole argument text must be nothing but "..." literals joined by `+`
# (and whitespace) -- anything else (e.g. `+ std::string(400, 'Z') +`) is a
# computed expression STRING_LIT_RE.findall would silently skip over rather
# than reject, which previously truncated a test's source without any error.
STRING_CONCAT_ONLY_RE = re.compile(
    r'^\s*(?:"(?:[^"\\]|\\.)*"\s*(?:\+\s*)?)+$'
)


def decode_concatenated_strings(s: str) -> str:
    """Decode a run of adjacent C++ "..." string literals. Only \\n and \\"
    escapes are in use in this file (confirmed by grep -- \\" appears in
    irContainsAll/irContainsNone pattern lists quoting an LLVM IR string,
    e.g. "!DIGlobalVariable(name: \\"x\\""); anything else raises, rather
    than silently mishandling it)."""
    if not STRING_CONCAT_ONLY_RE.match(s):
        raise ValueError(f"non-literal (computed?) string expression: {s!r}")
    out = []
    for part in STRING_LIT_RE.findall(s):
        if "\\" in part and not re.fullmatch(r'(\\n|\\"|[^\\])*', part):
            raise ValueError(f"unexpected escape in string literal: {part!r}")
        out.append(part.replace("\\n", "\n").replace('\\"', '"'))
    return "".join(out)


# `kEP + "literal"` / `std::string(kEP) + "literal"` -- the right side must
# still be pure string-literal concatenation (decode_concatenated_strings
# enforces this), so a loop-parametrized `kEP + " " + O` (a real identifier,
# not a literal) correctly raises here too -- moot in practice, since the
# SKIP_MARKERS for-loop check already routes that case to manual conversion
# before extract_call_args ever runs.
KEP_PLUS_RE = re.compile(r'^(?:std::string\(kEP\)|kEP)\s*\+\s*(.+)$', re.S)

# The reverse order -- "-g " + kEP (kEP is the trailing term, a literal
# PREFIX comes first) -- e.g. driver_test.cpp's two issue #19 shadowing
# tests, which need -g AND -std=iso10206 together. Only the simple
# literal-then-kEP two-term shape is handled; anything with a third term
# falls through to "unrecognized," same fail-loud-and-skip default as
# every other unrecognized flags shape.
LITERAL_PLUS_KEP_RE = re.compile(r'^(.+)\+\s*kEP\s*$', re.S)

CALL_RE = re.compile(r"(compileAndRun|compileAndEmitIR|compileTwoFiles|compileThreeFiles)\(")

# compileTwoFiles(ModSrc, ProgSrc, ExtraFlags="") / compileThreeFiles(ModASrc,
# ModBSrc, ProgSrc, ExtraFlags="") -- how many leading arguments are SOURCE
# text (as opposed to the trailing, optional ExtraFlags).
MODULE_CALL_SOURCE_COUNT = {"compileTwoFiles": 2, "compileThreeFiles": 3}


def resolve_flags_arg(flag_arg: str) -> str | None:
    """Shared by every call kind: kEP -> "-std=iso10206", a plain "..."
    literal used verbatim, kEP + "literal" / std::string(kEP) + "literal"
    resolved the same way with the literal suffix appended (see
    KEP_PLUS_RE), anything else raises rather than guessing."""
    # LITERAL_PLUS_KEP_RE and KEP_PLUS_RE are both checked before the plain
    # startswith('"') branch: "-g " + kEP starts with a literal quote
    # character too (it's " -g " followed by + kEP), so a naive
    # startswith('"') check would wrongly claim it as a plain literal and
    # feed the whole "-g " + kEP text to decode_concatenated_strings, which
    # correctly rejects it as non-literal -- but for the wrong reason, and
    # before ever reaching the dedicated handling below. Found for real
    # while generalizing this script to driver_test.cpp's two issue #19
    # shadowing tests.
    flags: str | None
    if flag_arg == "kEP":
        flags = "-std=iso10206"
    elif (m := KEP_PLUS_RE.match(flag_arg)):
        suffix = decode_concatenated_strings(m.group(1).strip())
        if "\n" in suffix:
            raise ValueError(f"multi-line flags argument: {suffix!r}")
        flags = "-std=iso10206" + suffix
    elif (m := LITERAL_PLUS_KEP_RE.match(flag_arg)):
        prefix = decode_concatenated_strings(m.group(1).strip())
        if "\n" in prefix:
            raise ValueError(f"multi-line flags argument: {prefix!r}")
        flags = prefix + "-std=iso10206"
    elif flag_arg.startswith('"'):
        flags = decode_concatenated_strings(flag_arg)
        if "\n" in flags:
            raise ValueError(f"multi-line flags argument: {flags!r}")
    else:
        raise ValueError(f"unrecognized (computed?) flags argument: {flag_arg!r}")
    return flags if flags != "" else None


def extract_call_args(body: str):
    """Find the first compileAndRun/compileAndEmitIR/compileTwoFiles/
    compileThreeFiles( call and return (call_kind, source_text_or_list,
    flags_text_or_None, stdin_text_or_None, remainder_after_call).
    flags_text is literal compiler-flags text (kEP resolved), never a
    %plang-vs-%plang_ep substitution choice -- see module docstring.
    For compileTwoFiles/compileThreeFiles, source_text is a LIST of 2 or 3
    decoded sources (module chunk(s) then the program) and stdin_text is
    always None -- DriverHarness.h's TwoFileResult helpers take no stdin
    argument at all. For compileAndRun/compileAndEmitIR, raises if a 3rd
    (StdinText) argument is present (needs manual conversion -- piping
    multi-line stdin content through lit's internal shell safely is not
    worth the mechanical-conversion complexity for the handful of cases
    that need it)."""
    m = CALL_RE.search(body)
    if not m:
        return None
    call_kind = m.group(1)
    i = m.end()  # just after the opening '('
    depth = 1
    in_str = False
    arg_start = i
    args = []
    while i < len(body):
        c = body[i]
        if c == '"':
            in_str = not in_str
        elif not in_str:
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
                if depth == 0:
                    args.append(body[arg_start:i])
                    i += 1
                    break
            elif c == "," and depth == 1:
                args.append(body[arg_start:i])
                arg_start = i + 1
        i += 1
    else:
        raise ValueError("unterminated call")

    if not args:
        raise ValueError(f"{call_kind} with no arguments")

    if call_kind in MODULE_CALL_SOURCE_COUNT:
        n_src = MODULE_CALL_SOURCE_COUNT[call_kind]
        if len(args) not in (n_src, n_src + 1):
            raise ValueError(f"{call_kind} with unexpected argument count: {len(args)}")
        sources = [decode_concatenated_strings(a) for a in args[:n_src]]
        flags = resolve_flags_arg(args[n_src].strip()) if len(args) == n_src + 1 else None
        return call_kind, sources, flags, None, body[i:]

    src = decode_concatenated_strings(args[0])

    if len(args) >= 4:
        raise ValueError("more arguments than compileAndRun/compileAndEmitIR take")

    flags = None
    if len(args) >= 2:
        flags = resolve_flags_arg(args[1].strip())

    stdin_text = None
    if len(args) == 3:
        stdin_arg = args[2].strip()
        if not stdin_arg.startswith('"'):
            raise ValueError(f"unrecognized (computed?) stdin argument: {stdin_arg!r}")
        stdin_text = decode_concatenated_strings(stdin_arg)

    return call_kind, src, flags, stdin_text, body[i:]


def extract_str_list(text: str) -> list[str]:
    """Return the double-quoted string literals in a {"...", ...}
    initializer list. `text` is already brace-delimited by the caller's own
    `\\{[^}]*\\}` match (see the one call site), with no nested braces
    possible, so a plain STRING_LIT_RE.findall over it is sufficient --
    unlike the nested-quantifier regex this replaced
    (`\\{\\s*((?:"..."\\s*,?\\s*)+)\\}`), which CodeQL flagged for
    exponential-backtracking blowup on long runs of whitespace between
    adjacent `\\s*`/`,?\\s*` repetitions."""
    items = STRING_LIT_RE.findall(text)
    if not items:
        raise ValueError('expected a {"...", ...} list')
    return items


def kebab_case(name: str) -> str:
    s = re.sub(r"(?<!^)(?=[A-Z])", "-", name)
    return s.lower()


def flags_prefix(flags: str | None) -> str:
    """Literal compiler-flags text with a trailing space, or "" if none --
    interpolated directly after the RUN-line base (%plang/%plang_ep/%plang_ir)
    in every RUN line below."""
    return f"{flags} " if flags else ""


def base_and_flags(suite: str, flags: str | None) -> tuple[str, str]:
    """(base, fp) -- base is normally "%plang"; for the ep suite specifically,
    when flags is known (by literal prefix match) to start with the dialect
    flag, switches to "%plang_ep" and strips that literal prefix out of fp --
    a pure DRY win (every ep RUN line otherwise repeats "-std=iso10206"),
    never a behavior change: %plang_ep already expands to "%plang
    -std=iso10206" (test/lit.cfg.py), so dropping the now-redundant
    literal copy is exactly equivalent. Falls back to %plang with flags
    untouched whenever the prefix isn't there (some ep cases legitimately
    omit the dialect flag, e.g. testing ISO 7185-under-EP-binary behavior) --
    never silently drops anything, matching this script's own standing rule."""
    if suite == "ep" and flags and flags.startswith("-std=iso10206"):
        rest = flags[len("-std=iso10206"):].lstrip()
        return "%plang_ep", (f"{rest} " if rest else "")
    return "%plang", flags_prefix(flags)


def module_compile_lines_and_body(base: str, fp: str, sources: list[str]):
    """The compileTwoFiles/compileThreeFiles shape: 2 or 3 source chunks,
    the last always the program, the rest module(s) compiled separately
    with -c and linked in with -I. Chunk names (mod.pas/prog.pas or
    moda.pas/modb.pas/prog.pas) are copied verbatim from DriverHarness.h's
    own convention -- confirmed by reading lib/Frontend/Frontend.cpp
    directly that a module's .pmi filename is always derived from its
    DECLARED name, never the source filename, so these names are entirely
    arbitrary and never need to match anything inside the source text."""
    if len(sources) == 2:
        chunk_names = ["mod.pas", "prog.pas"]
    elif len(sources) == 3:
        chunk_names = ["moda.pas", "modb.pas", "prog.pas"]
    else:
        raise ValueError(f"unexpected module source count: {len(sources)}")
    mod_chunks, prog_chunk = chunk_names[:-1], chunk_names[-1]
    compile_line = "RUN: split-file %s %t.dir\n"
    for chunk in mod_chunks:
        obj = chunk[: -len(".pas")] + ".o"
        compile_line += f"RUN: {base} {fp}-c %t.dir/{chunk} -o %t.dir/{obj}\n"
    obj_args = " ".join(f"%t.dir/{c[: -len('.pas')]}.o" for c in mod_chunks)
    compile_line += f"RUN: {base} {fp}-I%t.dir %t.dir/{prog_chunk} {obj_args} -o %t\n"
    body = "\n".join(
        f"//--- {chunk}\n" + src.rstrip("\n") + "\n"
        for chunk, src in zip(chunk_names, sources)
    )
    return compile_line, "%run %t", body


# A program declaring a `module` writes a .pmi beside whatever file the
# compiler actually compiles (confirmed by reading lib/Frontend/Frontend.cpp
# directly) -- compiling %s in place would write that .pmi into the
# checked-in source tree itself, on every test run. Matched module-first,
# case-insensitive (Pascal keywords are), multiline so it fires wherever a
# unit boundary ("end.\n") is followed by another "module" declaration, not
# just at the very start of the source.
DECLARES_MODULE_RE = re.compile(r"(?im)^\s*module\b")


def compile_lines_and_body(base: str, fp: str, src, stdin_text: str | None, compile_stderr_to: str | None = None):
    """Returns (compile_run_line, exec_prefix, body_text) for a case that
    compiles %s and then runs the result via %run (PLANG_TEST_RUN_WRAPPER /
    guardheap, per test/README.md's documented convention -- the
    Phase-2 script this was generalized from omitted this, a gap only
    caught and swept-fixed after the fact across 282 files in commit
    c964d54; baked in from the start here instead of repeating that).
    `src` is a list of 2-3 sources for compileTwoFiles/compileThreeFiles
    (see module_compile_lines_and_body), otherwise a single source string.
    Without stdin AND without a `module` declaration, the single-source
    case is the plain single-file idiom every other shape uses -- compiling
    %s directly. Either stdin or a module declaration routes through
    split-file instead (already the proven mechanism for multi-file Module
    tests, per this project's own design work) to carry the source (plus
    stdin content, if any) in one .pas file, `test.pas` + `stdin.txt` parts
    -- keeping the CHECK block in the PREAMBLE, before the first '//--- '
    marker, is required: content after the LAST marker is appended into
    that final part (confirmed empirically during design), which would
    silently corrupt stdin.txt with trailing CHECK directives if they were
    placed after it instead.
    `compile_stderr_to`, when given, redirects the COMPILE step's own
    stderr to that path -- needed by any caller that reproduces
    DriverHarness.h's real R.Stderr semantics (`R.Stderr = read
    ("compile.err"); ...; R.Stderr += read("run.err")`, confirmed by direct
    read of compileAndRunFile -- compile-time output, e.g. a warning on an
    otherwise-successful compile, is concatenated with runtime stderr, not
    discarded). Omitting it (every caller that doesn't need the compile
    step's own stderr) is unaffected -- found for real running
    Warnings-suite cases through real lit: a warning is emitted at COMPILE
    time even though the program compiles and runs successfully, and a
    builder that only captured the RUN step's stderr saw an empty file."""
    stderr_redirect = f" 2> {compile_stderr_to}" if compile_stderr_to else ""
    if isinstance(src, list):
        if compile_stderr_to:
            raise ValueError("compile_stderr_to is not supported for module-shaped compiles")
        return module_compile_lines_and_body(base, fp, src)
    if stdin_text is None and not DECLARES_MODULE_RE.search(src):
        compile_line = f"RUN: {base} {fp}%s -o %t{stderr_redirect}\n"
        return compile_line, "%run %t", src.rstrip("\n") + "\n"
    if stdin_text is None:
        compile_line = f"RUN: split-file %s %t.dir\nRUN: {base} {fp}%t.dir/test.pas -o %t{stderr_redirect}\n"
        return compile_line, "%run %t", "//--- test.pas\n" + src.rstrip("\n") + "\n"
    compile_line = f"RUN: split-file %s %t.dir\nRUN: {base} {fp}%t.dir/test.pas -o %t{stderr_redirect}\n"
    exec_prefix = "%run %t < %t.dir/stdin.txt"
    body = (
        "//--- test.pas\n"
        + src.rstrip("\n")
        + "\n\n//--- stdin.txt\n"
        + stdin_text
    )
    return compile_line, exec_prefix, body


def build_ir_substring_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    if call_kind != "compileAndEmitIR":
        return None
    if "irContainsAll" not in remainder and "irContainsNone" not in remainder:
        return None
    fp = flags_prefix(flags)
    checks = []
    for m in re.finditer(r"irContains(All|None)\(R\.IR,\s*(\{[^}]*\})\)", remainder):
        kind, list_text = m.groups()
        items = extract_str_list(list_text)
        directive = "CHECK-DAG" if kind == "All" else "CHECK-NOT"
        for item in items:
            # Same \n/\" allowlist as decode_concatenated_strings -- an IR
            # pattern quoting an LLVM IR string (e.g.
            # "!DIGlobalVariable(name: \"x\"") needs \" unescaped, not
            # rejected. extract_str_list's STRING_LIT_RE leaves escapes raw
            # (doesn't unescape), unlike decode_concatenated_strings's own
            # findall path, so this loop does its own equivalent pass.
            if "\\" in item and not re.fullmatch(r'(\\n|\\"|[^\\])*', item):
                raise ValueError(f"unexpected escape in IR pattern: {item!r}")
            checks.append(f"{directive}: " + item.replace("\\n", "\n").replace('\\"', '"'))
    if not checks:
        raise ValueError("irContainsAll/None found but no patterns extracted")
    # %plang_ir, not %plang: compileAndEmitIR's GTest harness never read
    # PLANG_TEST_EXTRA_FLAGS (see lit.cfg.py's own comment), so the checked
    # IR patterns below are unoptimized-shape by design -- %plang would
    # silently make them -O1/-O2/-O3-sensitive under the `optimized` CI job.
    run = f"(*\nRUN: %plang_ir {fp}-emit-llvm %s -o %t.ll\nRUN: FileCheck %s < %t.ll\n*)\n\n"
    return run + src.rstrip("\n") + "\n\n(*\n" + "\n".join(checks) + "\n*)\n"


EXACT_STDOUT_RE = re.compile(r'EXPECT_EQ\(R\.Stdout,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')

# Anchored on the macro name, not just the "ExitCode, 0)" substring -- that
# bare substring is ALSO present inside EXPECT_NE(R.ExitCode, 0) /
# ASSERT_NE(R.ExitCode, 0) (a rejection test), which previously let a
# rejection case with EXPECT_EQ(R.Stdout, "") reach EXACT_STDOUT_RE, match
# an empty string, and raise ("empty expected stdout") -- aborting the
# whole BUILDERS chain before build_nonzero_exit_case (the actually correct
# builder for that case) ever ran. Found for real generalizing this script
# to ep_test.cpp, where ASSERT_EQ (not just EXPECT_EQ) is the dominant
# ExitCode idiom -- both accepted here, same as the original bare-substring
# check already (accidentally) did.
EXIT_CODE_ZERO_RE = re.compile(r"(?:EXPECT|ASSERT)_EQ\(R\.ExitCode,\s*0\)")


def build_exact_stdout_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    if call_kind not in ("compileAndRun", "compileTwoFiles", "compileThreeFiles"):
        return None
    if not EXIT_CODE_ZERO_RE.search(remainder):
        return None
    m = EXACT_STDOUT_RE.search(remainder)
    if not m:
        return None
    stdout = decode_concatenated_strings(m.group(1))
    if stdout == "":
        raise ValueError("empty expected stdout, nothing to CHECK")
    lines = stdout.split("\n")
    if lines and lines[-1] == "":
        lines.pop()  # trailing '\n' produces one empty trailing element
    if not lines:
        raise ValueError("no output lines to check")
    # No space inserted between "CHECK:"/"CHECK-NEXT:" and the line content:
    # under --strict-whitespace (needed so internal padding differences, e.g.
    # a wrong field width, are actually caught), that separator is NOT an
    # auto-stripped delimiter the way it is in FileCheck's default mode --
    # it becomes part of the literal pattern. "CHECK: X" only matches a line
    # that IS " X" (with a real leading space); a plain "X" line needs
    # "CHECK:X" with nothing between the colon and the content. Confirmed
    # empirically (a naive "CHECK: " space caused the overwhelming majority
    # of this batch's first extraction attempt to fail against perfectly
    # correct output) before trusting this fix and regenerating everything.
    if any(l == "" for l in lines):
        raise ValueError("a blank expected output line -- FileCheck rejects an empty CHECK pattern")
    checks = [f"CHECK:{lines[0]}"] + [f"CHECK-NEXT:{l}" for l in lines[1:]]
    base, fp = base_and_flags(suite, flags)
    # A successful run can ALSO assert something about stderr: a specific
    # message present (EXPECT_NE(R.Stderr.find(...), npos)) and/or a
    # specific message absent (EXPECT_EQ(R.Stderr.find(...), npos)) --
    # e.g. "produces this exact output, AND warns about X" or "... AND
    # must not warn about Y". Dropping the PRESENT half silently was a
    # real, confirmed-live bug in an earlier version of this builder (it
    # read STDERR_NOT_FIND_RES but never STDERR_FIND_RES) -- found for
    # real generalizing this script to driver_test.cpp, where the exact-
    # stdout-plus-positive-stderr-substring combination first occurs (no
    # prior migrated suite ever exercised it). When either is present,
    # stdout/stderr are captured to separate files and checked with
    # separate FileCheck passes rather than piping stdout alone.
    err_msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    err_not_msgs = find_all_substring_checks(remainder, STDERR_NOT_FIND_RES)
    if err_msgs or err_not_msgs:
        # R.Stderr is compile-time stderr CONCATENATED with run-time stderr
        # (confirmed by reading DriverHarness.h's compileAndRunFile
        # directly) -- a warning on an otherwise-successful compile lands
        # in the COMPILE step's own stderr, not the run step's. Capture
        # both and `cat` them together into %t.err, matching R.Stderr's
        # real semantics -- found for real running a Warnings-suite case
        # through real lit with only the run step's (empty) stderr
        # captured.
        compile_line, exec_prefix, body = compile_lines_and_body(
            base, fp, src, stdin_text, compile_stderr_to="%t.compile.err"
        )
        run = (
            f"(*\n{compile_line}"
            f"RUN: {exec_prefix} > %t.out 2> %t.run.err\n"
            f"RUN: cat %t.compile.err %t.run.err > %t.err\n"
            f"RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out\n"
            + (f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n" if err_msgs else "")
            + (f"RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
            + "*)\n\n"
        )
        err_checks = (
            f"ERR: {err_msgs[0]}" if len(err_msgs) == 1 else "\n".join(f"ERR-DAG: {m}" for m in err_msgs)
        )
        err_absent_checks = "\n".join(f"ERR-ABSENT-NOT: {m}" for m in err_not_msgs)
        all_checks = "\n".join(checks)
        if err_msgs:
            all_checks += "\n" + err_checks
        if err_not_msgs:
            all_checks += "\n" + err_absent_checks
        return run + "(*\n" + all_checks + "\n*)\n\n" + body
    compile_line, exec_prefix, body = compile_lines_and_body(base, fp, src, stdin_text)
    run = (
        f"(*\n{compile_line}"
        f"RUN: {exec_prefix} | FileCheck --strict-whitespace --match-full-lines %s\n*)\n\n"
    )
    return run + "(*\n" + "\n".join(checks) + "\n*)\n\n" + body


# Two idioms for the same "substring present" check are both in use:
#   EXPECT_NE(X.find("..."), std::string::npos)
#   EXPECT_TRUE(X.find("...") != std::string::npos)
# and their negations -- "substring ABSENT" -- which share the identical
# `X.find("..."), std::string::npos)` tail and are only distinguished by
# which EXPECT_ macro wraps them. An earlier version of this script matched
# on the tail alone, so an EXPECT_EQ(...find(...), npos) "must NOT appear"
# assertion was silently treated as a positive CHECK-DAG -- confirmed to
# have actually happened for 3 real cases (SemaDiagnostics, ForwardDecl,
# NonLocalGoto) before the macro name was anchored into the regex.
def _find_ne_re(var):
    return re.compile(
        r"EXPECT_NE\(\s*" + var + r'\.find\(((?:"(?:[^"\\]|\\.)*"\s*)+)\)\s*,\s*std::string::npos\s*\)'
    )


def _find_eq_re(var):
    return re.compile(
        r"EXPECT_EQ\(\s*" + var + r'\.find\(((?:"(?:[^"\\]|\\.)*"\s*)+)\)\s*,\s*std::string::npos\s*\)'
    )


def _find_true_re(var):
    return re.compile(
        r"EXPECT_TRUE\(\s*" + var + r'\.find\(((?:"(?:[^"\\]|\\.)*"\s*)+)\)\s*!=\s*std::string::npos'
    )


def _find_false_re(var):
    return re.compile(
        r"EXPECT_FALSE\(\s*" + var + r'\.find\(((?:"(?:[^"\\]|\\.)*"\s*)+)\)\s*!=\s*std::string::npos'
    )


STDERR_FIND_RES = [_find_ne_re(r"R\.Stderr"), _find_true_re(r"R\.Stderr")]
STDOUT_FIND_RES = [_find_ne_re(r"R\.Stdout"), _find_true_re(r"R\.Stdout")]
STDERR_NOT_FIND_RES = [_find_eq_re(r"R\.Stderr"), _find_false_re(r"R\.Stderr")]
STDOUT_NOT_FIND_RES = [_find_eq_re(r"R\.Stdout"), _find_false_re(r"R\.Stdout")]


def find_all_substring_checks(remainder: str, patterns) -> list[str]:
    """Every caller interpolates the result directly into a single `CHECK:
    <msg>` line -- a decoded message containing a literal embedded newline
    (e.g. CaretDiagnostics.IndentsTheCaretWithTheLinesOwnWhitespace's
    R.Stderr.find("\\n             ^")) would silently split the FileCheck
    directive across two lines in the generated file, corrupting it into an
    empty CHECK plus a stray non-directive line -- found for real running
    the generated .pas file through real lit/FileCheck, not from reading
    the regex alone. Rejected here rather than silently mishandled; the
    rare multi-line-substring case needs its own CHECK/CHECK-NEXT pair,
    hand-authored."""
    msgs = []
    for pat in patterns:
        for m in pat.finditer(remainder):
            msg = decode_concatenated_strings(m.group(1))
            if "\n" in msg:
                raise ValueError(f"substring check spans multiple lines: {msg!r}")
            msgs.append(msg)
    return msgs


NONZERO_EXIT_RE = re.compile(r"(?:NE\(R\.ExitCode,\s*0\)|EQ\(R\.ExitCode,\s*[1-9]\d*\))")


def build_nonzero_exit_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    # Deliberately excludes compileTwoFiles/compileThreeFiles: compile_succeeds
    # below assumes a single source string it can write to one file and
    # compile in one step, which doesn't fit the module-then-program shape,
    # and of the module suite's genuinely-failing cases every one fails at
    # the PROGRAM-compile step specifically -- not worth generalizing this
    # builder for the handful of cases that need it; hand-convert them.
    if call_kind != "compileAndRun":
        return None
    if not NONZERO_EXIT_RE.search(remainder):
        return None
    msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    if not msgs:
        return None  # no message check -- flagged for manual review upstream
    base, fp = base_and_flags(suite, flags)
    # Each original .find() call was checked independently, not in a
    # sequence dependent on the others' position, so multiple stderr
    # substrings are exactly as order-independent as multiple stdout ones.
    is_runtime = compile_succeeds(src, flags)
    out_msgs = find_all_substring_checks(remainder, STDOUT_FIND_RES)
    # "Must NOT appear anywhere" checks. Run as their own FileCheck pass
    # (own check-prefix, only CHECK-NOT lines) rather than folding into the
    # ERR pass's CHECK-DAG group -- CHECK-NOT's "not found between the
    # previous and next directive" scoping only guarantees "not found
    # anywhere in the file" when there is no other directive sharing its
    # prefix, which a standalone pass gives for free without relying on
    # CHECK-DAG/CHECK-NOT ordering interactions.
    err_not_msgs = find_all_substring_checks(remainder, STDERR_NOT_FIND_RES)
    out_not_msgs = find_all_substring_checks(remainder, STDOUT_NOT_FIND_RES)
    if out_not_msgs and not is_runtime:
        raise ValueError("stdout check on a compile-time rejection is unexpected")

    if is_runtime:
        # R.Stderr = compile.err + run.err (see build_exact_stdout_case's
        # identical note) -- a checked message could in principle come
        # from either half, so both are captured and concatenated rather
        # than assuming a runtime failure never also warns at compile time.
        compile_line, exec_prefix, body = compile_lines_and_body(
            base, fp, src, stdin_text, compile_stderr_to="%t.compile.err"
        )
        run = (
            f"(*\n{compile_line}"
            f"RUN: not {exec_prefix} > %t.out 2> %t.run.err\n"
            f"RUN: cat %t.compile.err %t.run.err > %t.err\n"
            f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n"
            + ("RUN: FileCheck --check-prefix=OUT %s < %t.out\n" if out_msgs else "")
            + ("RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
            + ("RUN: FileCheck --check-prefix=OUT-ABSENT %s < %t.out\n" if out_not_msgs else "")
            + "*)\n\n"
        )
    else:
        if out_msgs:
            raise ValueError("stdout check on a compile-time rejection is unexpected")
        # stdin_text (if any) is irrelevant here: a failed compile never runs
        # anything, matching compileAndRunFile's own short-circuit -- same as
        # the original C++ test's own unused 3rd argument in this case.
        body = src.rstrip("\n") + "\n"
        run = (
            f"(*\nRUN: not {base} {fp}%s -o %t 2> %t.err\n"
            f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n"
            + ("RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
            + "*)\n\n"
        )
    err_checks = (
        f"ERR: {msgs[0]}" if len(msgs) == 1 else "\n".join(f"ERR-DAG: {m}" for m in msgs)
    )
    out_checks = "\n".join(f"OUT-DAG: {m}" for m in out_msgs)
    err_absent_checks = "\n".join(f"ERR-ABSENT-NOT: {m}" for m in err_not_msgs)
    out_absent_checks = "\n".join(f"OUT-ABSENT-NOT: {m}" for m in out_not_msgs)
    checks = err_checks + (("\n" + out_checks) if out_checks else "")
    checks += ("\n" + err_absent_checks) if err_absent_checks else ""
    checks += ("\n" + out_absent_checks) if out_absent_checks else ""
    return run + "(*\n" + checks + "\n*)\n\n" + body


def build_stdout_substring_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    """EXPECT_NE(R.Stdout.find(...), npos) / EXPECT_TRUE(R.Stdout.find(...) !=
    npos) -- the original assertion was ALREADY a substring search, not an
    exact match, so plain (order-independent) CHECK-DAG is the faithful
    translation, not --strict-whitespace/--match-full-lines/CHECK-NEXT."""
    if call_kind not in ("compileAndRun", "compileTwoFiles", "compileThreeFiles"):
        return None
    if not EXIT_CODE_ZERO_RE.search(remainder):
        return None
    msgs = find_all_substring_checks(remainder, STDOUT_FIND_RES)
    if not msgs:
        return None
    base, fp = base_and_flags(suite, flags)
    checks = "\n".join(f"CHECK-DAG: {m}" for m in msgs)
    # Same stderr blind spot build_exact_stdout_case had (see its comment,
    # including the R.Stderr = compile.err + run.err concatenation this
    # also needs) -- this builder never consulted STDERR_FIND_RES/
    # STDERR_NOT_FIND_RES at all. No driver_test.cpp case currently
    # exercises this exact combination, but the fix costs nothing to carry
    # here too rather than leave the same latent gap for the next suite to
    # trip over.
    err_msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    err_not_msgs = find_all_substring_checks(remainder, STDERR_NOT_FIND_RES)
    if err_msgs or err_not_msgs:
        compile_line, exec_prefix, body = compile_lines_and_body(
            base, fp, src, stdin_text, compile_stderr_to="%t.compile.err"
        )
        run = (
            f"(*\n{compile_line}"
            f"RUN: {exec_prefix} > %t.out 2> %t.run.err\n"
            f"RUN: cat %t.compile.err %t.run.err > %t.err\n"
            f"RUN: FileCheck %s < %t.out\n"
            + (f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n" if err_msgs else "")
            + (f"RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
            + "*)\n\n"
        )
        err_checks = (
            f"ERR: {err_msgs[0]}" if len(err_msgs) == 1 else "\n".join(f"ERR-DAG: {m}" for m in err_msgs)
        )
        err_absent_checks = "\n".join(f"ERR-ABSENT-NOT: {m}" for m in err_not_msgs)
        all_checks = checks
        if err_msgs:
            all_checks += "\n" + err_checks
        if err_not_msgs:
            all_checks += "\n" + err_absent_checks
        return run + "(*\n" + all_checks + "\n*)\n\n" + body
    compile_line, exec_prefix, body = compile_lines_and_body(base, fp, src, stdin_text)
    run = f"(*\n{compile_line}RUN: {exec_prefix} | FileCheck %s\n*)\n\n"
    return run + "(*\n" + checks + "\n*)\n\n" + body


def build_emit_ir_rejection_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    """compileAndEmitIR + EXPECT_FALSE(R.Ok) -- a Sema/parse rejection
    detected on the -emit-llvm path rather than compileAndRun's plain
    compile. Any secondary R.IR.find(...) check on a build that failed is
    dropped (there is no %t.ll to inspect once -emit-llvm itself fails) --
    the primary, meaningful assertion (rejected, with this diagnostic) is
    what's preserved."""
    if call_kind != "compileAndEmitIR":
        return None
    if "EXPECT_FALSE(R.Ok)" not in remainder:
        return None
    msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    if len(msgs) != 1:
        return None
    fp = flags_prefix(flags)
    # %plang_ir, not %plang -- see build_ir_substring_case's identical note.
    run = (
        f"(*\nRUN: not %plang_ir {fp}-emit-llvm %s -o %t.ll 2> %t.err\n"
        f"RUN: FileCheck %s < %t.err\n*)\n\n"
    )
    return run + src.rstrip("\n") + f"\n\n(*\nCHECK: {msgs[0]}\n*)\n"


# runPlang/runPC1/runPlangRc: invokes the plang binary directly with a raw
# command-line-args string and inspects its combined stdout+stderr (2>&1,
# per DriverHarness.h) -- no Pascal source, no compileAndRun/compileAndEmitIR
# call at all, so CALL_RE never sees these. A separate recognizer, not a
# CALL_RE extension: the argument shape (a raw args string, not Pascal
# source) and the RUN-line shape (%plang directly, never a two-step
# compile-then-run) differ enough that folding this into extract_call_args
# would obscure both. Declaration shapes actually in use (confirmed by
# grep, not assumed): `std::string X = runPlang(...)`, `const std::string X
# = runPC1(...)`, `auto [Rc, Out] = runPlangRc(...)`.
CLI_CALL_RE = re.compile(
    r'(?:auto\s*\[\s*(\w+)\s*,\s*(\w+)\s*\]'
    r'|(?:std::string|const\s+std::string|auto)\s+(\w+))'
    r'\s*=\s*run(Plang|PC1|PlangRc)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)*)\)'
)


def build_cli_invocation_case(body: str):
    """Returns a .pas file body, or None (falls through to manual
    conversion -- always the safe default for anything not confidently
    recognized here)."""
    calls = list(CLI_CALL_RE.finditer(body))
    if len(calls) != 1:
        return None  # 0 -> not this shape; >1 -> needs manual (comparing two invocations)
    m = calls[0]
    rc_var, out_var_binding, out_var_plain, kind, arg_text = m.groups()
    out_var = out_var_binding or out_var_plain
    if not arg_text.strip():
        args = ""
    else:
        args = decode_concatenated_strings(arg_text)
    # %plang_ir, not %plang: runPlang/runPC1/runPlangRc never read
    # PLANG_TEST_EXTRA_FLAGS either (confirmed by direct read of
    # DriverHarness.h -- same exemption compileAndEmitIR/compileFileToIR
    # already has, and %plang_ir's own name is misleading here but its
    # VALUE is exactly "bare plang_exe, no extra flags", which is what
    # matters). Using bare %plang would silently make `--help`/diagnostic
    # text sensitive to the `optimized` CI job's -O1/-O2/-O3 re-run --
    # found for real: `-pc1 --help` prefixed with a stray `-O1` changed
    # which help text (front-end-only vs. full driver) got printed.
    base = "%plang_ir -pc1" if kind == "PC1" else "%plang_ir"
    remainder = body[m.end():]

    # runPlang/runPC1 (no rc_var) discard the real process exit code
    # entirely -- DriverHarness.h's own implementation never captures it,
    # so the ORIGINAL C++ test has no way to assert on it either, and the
    # real invocation's exit status is genuinely unobservable/unasserted.
    # A bare (no `not`, no suffix) RUN line would still make lit fail the
    # whole line the moment the real command happens to exit nonzero (e.g.
    # any "file not found"/diagnostic-producing invocation) even though
    # the C++ test never cared -- found for real running the generated
    # .pas file through real lit (AnErrorWithNoPlaceInTheSourcePrintsNoSnippet
    # legitimately invokes a failing compile via runPC1). `; true` after
    # the command is this project's own established idiom for "tolerate
    # whichever exit status happens, only the checks below are real
    # assertions" (see RuntimeChecks/no-nil-checks-flag-omits-it.pas).
    # Exact pinned exit code is dropped for a nonzero rc_var value (lit's
    # internal shell has no $? expansion -- same established precedent as
    # the Halt exit-status case), kept only as a plain not/no-not gate.
    exit_prefix, exit_suffix = "", ""
    if rc_var:
        if re.search(rf"(?:EXPECT|ASSERT)_EQ\(\s*{re.escape(rc_var)}\s*,\s*0\s*\)", remainder):
            pass  # bare command must exit 0 -- a faithful gate, no `not` needed
        elif re.search(rf"(?:EXPECT|ASSERT)_(?:EQ|NE)\(\s*{re.escape(rc_var)}\s*,", remainder):
            exit_prefix = "not "
        else:
            exit_suffix = "; true"  # rc_var captured but never asserted on
    else:
        exit_suffix = "; true"

    var_re = re.escape(out_var)
    exact_m = re.search(
        rf'EXPECT_EQ\(\s*{var_re}\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', remainder
    )
    diag_m = re.search(
        rf'EXPECT_EQ\(\s*diagCount\(\s*{var_re}\s*\)\s*,\s*(\d+)\s*\)', remainder
    )
    present_msgs = find_all_substring_checks(remainder, [_find_ne_re(var_re), _find_true_re(var_re)])
    absent_msgs = find_all_substring_checks(remainder, [_find_eq_re(var_re), _find_false_re(var_re)])

    # Safety net: if the remainder has MORE `<var>.find(` call sites than
    # were actually captured above, something about this test's assertion
    # shape isn't recognized (e.g. a single-quoted char literal --
    # X.find('^') -- which _find_ne_re/_find_eq_re's regex, built for
    # double-quoted string literals, silently does not match at all).
    # Bail to manual rather than emit a file missing that assertion --
    # found for real: AnErrorWithNoPlaceInTheSourcePrintsNoSnippet checks
    # BOTH diagCount(Out) and Out.find('^'), and only the first was being
    # captured before this check existed.
    total_find_calls = len(re.findall(rf"{var_re}\.find\(", remainder))
    if total_find_calls > len(present_msgs) + len(absent_msgs):
        return None

    compile_line = (
        f"RUN: {exit_prefix}{base} {args} > %t.out 2>&1{exit_suffix}\n" if args
        else f"RUN: {exit_prefix}{base} > %t.out 2>&1{exit_suffix}\n"
    )

    if exact_m:
        out = decode_concatenated_strings(exact_m.group(1))
        if out == "":
            return None
        lines = out.split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        if not lines or any(l == "" for l in lines):
            return None
        checks = [f"CHECK:{lines[0]}"] + [f"CHECK-NEXT:{l}" for l in lines[1:]]
        run = f"(*\n{compile_line}RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out\n*)\n\n"
        return run + "(*\n" + "\n".join(checks) + "\n*)\n"
    if diag_m:
        run = (
            f"(*\n{compile_line}"
            f"RUN: grep -c 'error: ' %t.out | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s\n*)\n\n"
        )
        return run + f"(*\nCOUNT:{diag_m.group(1)}\n*)\n"
    if present_msgs or absent_msgs:
        run = (
            f"(*\n{compile_line}"
            + (f"RUN: FileCheck --check-prefix=OUT %s < %t.out\n" if present_msgs else "")
            + (f"RUN: FileCheck --allow-empty --check-prefix=OUT-ABSENT %s < %t.out\n" if absent_msgs else "")
            + "*)\n\n"
        )
        out_checks = (
            f"OUT: {present_msgs[0]}" if len(present_msgs) == 1 else "\n".join(f"OUT-DAG: {m}" for m in present_msgs)
        )
        out_absent_checks = "\n".join(f"OUT-ABSENT-NOT: {m}" for m in absent_msgs)
        checks = out_checks if present_msgs else ""
        if absent_msgs:
            checks += ("\n" if checks else "") + out_absent_checks
        return run + "(*\n" + checks + "\n*)\n"
    return None


def build_success_stderr_only_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    """compileAndRun with ONLY a stderr assertion (present and/or absent) and
    no stdout assertion at all -- e.g. a warning-text check with no
    expectation about program output. First seen at scale in
    driver_test.cpp's Warnings suite (31 cases); none of the other 4
    BUILDERS fire for this shape (build_exact_stdout_case/
    build_stdout_substring_case both require a stdout assertion to even
    match; build_nonzero_exit_case requires a nonzero-exit assertion).
    Two related C++ shapes reach this builder: ExitCode==0 explicitly
    asserted (a real compile+run, e.g. the Warnings suite, checking a
    message printed at COMPILE time on an otherwise-successful compile),
    or NO ExitCode assertion at all (e.g.
    DiagnosticLanguage.TheFlagReachesTheFrontEnd -- these deliberately
    compile an INVALID program, e.g. an undeclared variable, purely to
    trigger a diagnostic, and never look at the exit status at all).
    compile_succeeds() (the same ground-truth prober build_nonzero_exit_case
    already uses) distinguishes them empirically rather than trusting
    which C++ shape was used: when the compile genuinely fails, there is
    no %t to run at all, and attempting to anyway is actively wrong, not
    just redundant -- confirmed by running the generated .pas file through
    real lit: `%run %t` on a nonexistent %t reports "command not found"
    (exit 127) in a way `; true` does not reliably suppress the same way
    it does an ordinary nonzero *process* exit, unlike a ordinary compile
    failure on a program that WOULD otherwise run."""
    if call_kind != "compileAndRun":
        return None
    if NONZERO_EXIT_RE.search(remainder):
        return None  # a real nonzero-exit case -- build_nonzero_exit_case's job
    if re.search(r"R\.Stdout", remainder):
        return None  # stdout is asserted somewhere -- a different builder's job
    err_msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    err_not_msgs = find_all_substring_checks(remainder, STDERR_NOT_FIND_RES)
    if not err_msgs and not err_not_msgs:
        return None
    base, fp = base_and_flags(suite, flags)
    is_compilable = compile_succeeds(src, flags)
    # R.Stderr = compile.err + run.err (see build_exact_stdout_case's
    # identical note) -- this builder is ENTIRELY about stderr content, and
    # the whole point of most of its cases (the Warnings suite) is a
    # message printed at COMPILE time on an otherwise-successful compile,
    # so this is the one builder where getting this right matters most.
    if not is_compilable:
        if isinstance(src, list):
            raise ValueError("unexpected list src for compileAndRun")
        run = (
            f"(*\nRUN: {base} {fp}%s -o %t 2> %t.err; true\n"
            + (f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n" if err_msgs else "")
            + (f"RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
            + "*)\n\n"
        )
        body = src.rstrip("\n") + "\n"
        err_checks = (
            f"ERR: {err_msgs[0]}" if len(err_msgs) == 1 else "\n".join(f"ERR-DAG: {m}" for m in err_msgs)
        )
        err_absent_checks = "\n".join(f"ERR-ABSENT-NOT: {m}" for m in err_not_msgs)
        checks = err_checks if err_msgs else ""
        if err_not_msgs:
            checks += ("\n" if checks else "") + err_absent_checks
        return run + "(*\n" + checks + "\n*)\n\n" + body
    exit_suffix = "" if EXIT_CODE_ZERO_RE.search(remainder) else "; true"
    compile_line, exec_prefix, body = compile_lines_and_body(
        base, fp, src, stdin_text, compile_stderr_to="%t.compile.err"
    )
    run = (
        f"(*\n{compile_line}"
        f"RUN: {exec_prefix} > %t.out 2> %t.run.err{exit_suffix}\n"
        f"RUN: cat %t.compile.err %t.run.err > %t.err\n"
        + (f"RUN: FileCheck --check-prefix=ERR %s < %t.err\n" if err_msgs else "")
        + (f"RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err\n" if err_not_msgs else "")
        + "*)\n\n"
    )
    err_checks = (
        f"ERR: {err_msgs[0]}" if len(err_msgs) == 1 else "\n".join(f"ERR-DAG: {m}" for m in err_msgs)
    )
    err_absent_checks = "\n".join(f"ERR-ABSENT-NOT: {m}" for m in err_not_msgs)
    checks = err_checks if err_msgs else ""
    if err_not_msgs:
        checks += ("\n" if checks else "") + err_absent_checks
    return run + "(*\n" + checks + "\n*)\n\n" + body


BUILDERS = [
    build_ir_substring_case,
    build_exact_stdout_case,
    build_nonzero_exit_case,
    build_stdout_substring_case,
    build_success_stderr_only_case,
    build_emit_ir_rejection_case,
]

SKIP_MARKERS = ("for (", "while (")


def main() -> int:
    check_only = "--check" in sys.argv
    positional = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(positional) != 1 or positional[0] not in SUITES:
        print(f"usage: {sys.argv[0]} <{'|'.join(SUITES)}> [--check]", file=sys.stderr)
        return 2
    src_file, out_root = SUITES[positional[0]]
    text = src_file.read_text()
    written = 0
    skipped = []
    for suite, name, body in find_test_bodies(text):
        body = strip_line_comments(body)
        if any(marker in body for marker in SKIP_MARKERS):
            skipped.append((suite, name, "control flow in body"))
            continue
        n_calls = len(CALL_RE.findall(body))
        if n_calls > 1:
            skipped.append((suite, name, "multiple compile calls"))
            continue
        try:
            parsed = extract_call_args(body)
        except ValueError as e:
            skipped.append((suite, name, f"call-parse error: {e}"))
            continue

        pas = None
        if parsed is None:
            # No compileAndRun/compileAndEmitIR call -- try the CLI-invocation
            # shape (runPlang/runPC1/runPlangRc) before giving up.
            try:
                pas = build_cli_invocation_case(body)
            except ValueError as e:
                skipped.append((suite, name, f"cli build error: {e}"))
                continue
            if pas is None:
                skipped.append((suite, name, "no compileAndRun/compileAndEmitIR/CLI call"))
                continue
        else:
            call_kind, src, flags, stdin_text, remainder = parsed
            try:
                for builder in BUILDERS:
                    pas = builder(suite, name, call_kind, src, flags, stdin_text, remainder)
                    if pas is not None:
                        break
            except ValueError as e:
                skipped.append((suite, name, f"build error: {e}"))
                continue
            if pas is None:
                skipped.append((suite, name, "unrecognized assertion shape"))
                continue

        out_dir = out_root / suite
        out_path = out_dir / (kebab_case(name) + ".pas")
        if check_only:
            existing = out_path.read_text() if out_path.exists() else None
            if existing != pas:
                skipped.append((suite, name, "would change (run without --check)"))
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path.write_text(pas)
        written += 1

    print(f"{'Would write' if check_only else 'Wrote'} {written if not check_only else '?'} .pas files")
    print(f"Skipped {len(skipped)} (manual conversion needed):")
    for suite, name, reason in skipped:
        print(f"  {suite}.{name}: {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
