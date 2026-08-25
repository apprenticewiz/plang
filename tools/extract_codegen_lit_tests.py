#!/usr/bin/env python3
"""Extract test/Driver/codegen_test.cpp's GoogleTest cases into standalone
.pas files under test-lit/CodeGen/ (issue #34 Phase 2).

Unlike Phase 1's Conformance suite (machine-generated, perfectly uniform,
raw-string-delimited), codegen_test.cpp is hand-written with genuine
structural variety, and uses ordinary C++ string literals (auto-concatenated
across adjacent "..." tokens, \\n as the only escape in use -- confirmed by
grep before writing this) rather than raw strings. This script handles the
common, mechanically-convertible shapes and explicitly SKIPS (reports, does
not guess) anything with real control flow (for/while loops, more than one
compile call, a stdin-text third argument, a computed flags expression) or
an assertion shape it doesn't recognize -- those get converted by hand.

Flags handling: compileAndRun/compileAndEmitIR's second argument (ExtraFlags)
is NOT specially mapped to a %plang vs %plang_ep substitution choice -- it is
literal compiler flags text (kEP resolved to "-std=iso10206", everything else
used verbatim), interpolated directly after %plang. This was corrected after
an earlier draft silently dropped every non-dialect flag (-fno-range-checks
etc.) by only special-casing the dialect value -- found for real by grepping
every distinct second-argument value actually in use in this file, not
assumed from a smaller sample.

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
SRC_FILE = REPO_ROOT / "test" / "Driver" / "codegen_test.cpp"
OUT_ROOT = REPO_ROOT / "test-lit" / "CodeGen"
PLANG_BIN = REPO_ROOT / "build" / "bin" / "plang"


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
    """Decode a run of adjacent C++ "..." string literals (only \\n escapes
    are in use in this file -- confirmed by grep -- so that's all this
    handles; anything else raises, rather than silently mishandling it)."""
    if not STRING_CONCAT_ONLY_RE.match(s):
        raise ValueError(f"non-literal (computed?) string expression: {s!r}")
    out = []
    for part in STRING_LIT_RE.findall(s):
        if "\\" in part and not re.fullmatch(r"(\\n|[^\\])*", part):
            raise ValueError(f"unexpected escape in string literal: {part!r}")
        out.append(part.replace("\\n", "\n"))
    return "".join(out)


CALL_RE = re.compile(r"(compileAndRun|compileAndEmitIR)\(")


def extract_call_args(body: str):
    """Find the first compileAndRun/compileAndEmitIR( call and return
    (call_kind, source_text, flags_text_or_None, remainder_after_call).
    flags_text is literal compiler-flags text (kEP resolved), never a
    %plang-vs-%plang_ep substitution choice -- see module docstring.
    Raises if a 3rd (StdinText) argument is present (needs manual
    conversion -- piping multi-line stdin content through lit's internal
    shell safely is not worth the mechanical-conversion complexity for the
    handful of cases that need it)."""
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
        raise ValueError("compileAndRun/compileAndEmitIR with no arguments")
    src = decode_concatenated_strings(args[0])

    if len(args) >= 4:
        raise ValueError("more arguments than compileAndRun/compileAndEmitIR take")

    flags = None
    if len(args) >= 2:
        flag_arg = args[1].strip()
        if flag_arg == "kEP":
            flags = "-std=iso10206"
        elif flag_arg.startswith('"'):
            flags = decode_concatenated_strings(flag_arg)
            if "\n" in flags:
                raise ValueError(f"multi-line flags argument: {flags!r}")
        else:
            raise ValueError(f"unrecognized (computed?) flags argument: {flag_arg!r}")
        if flags == "":
            flags = None

    stdin_text = None
    if len(args) == 3:
        stdin_arg = args[2].strip()
        if not stdin_arg.startswith('"'):
            raise ValueError(f"unrecognized (computed?) stdin argument: {stdin_arg!r}")
        stdin_text = decode_concatenated_strings(stdin_arg)

    return call_kind, src, flags, stdin_text, body[i:]


STR_LIST_RE = re.compile(r"\{\s*((?:\"(?:[^\"\\]|\\.)*\"\s*,?\s*)+)\}")


def extract_str_list(text: str) -> list[str]:
    m = STR_LIST_RE.search(text)
    if not m:
        raise ValueError('expected a {"...", ...} list')
    return STRING_LIT_RE.findall(m.group(1))


def kebab_case(name: str) -> str:
    s = re.sub(r"(?<!^)(?=[A-Z])", "-", name)
    return s.lower()


def flags_prefix(flags: str | None) -> str:
    """Literal compiler-flags text with a trailing space, or "" if none --
    interpolated directly after %plang in every RUN line below."""
    return f"{flags} " if flags else ""


def compile_lines_and_body(fp: str, src: str, stdin_text: str | None):
    """Returns (compile_run_line, exec_prefix, body_text) for a case that
    compiles %s and then runs the result. Without stdin, this is the plain
    single-file idiom every other shape uses. With stdin, uses split-file
    (already the proven mechanism for multi-file Module tests, per this
    project's own design work) to carry the source AND the stdin content in
    one .pas file, `test.pas` + `stdin.txt` parts -- keeping the CHECK block
    in the PREAMBLE, before the first '//--- ' marker, is required: content
    after the LAST marker is appended into that final part (confirmed
    empirically during design), which would silently corrupt stdin.txt with
    trailing CHECK directives if they were placed after it instead."""
    if stdin_text is None:
        compile_line = f"RUN: %plang {fp}%s -o %t\n"
        return compile_line, "%t", src.rstrip("\n") + "\n"
    compile_line = f"RUN: split-file %s %t.dir\nRUN: %plang {fp}%t.dir/test.pas -o %t\n"
    exec_prefix = "%t < %t.dir/stdin.txt"
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
            if "\\" in item:
                raise ValueError(f"unexpected escape in IR pattern: {item!r}")
            checks.append(f"{directive}: {item}")
    if not checks:
        raise ValueError("irContainsAll/None found but no patterns extracted")
    run = f"(*\nRUN: %plang {fp}-emit-llvm %s -o %t.ll\nRUN: FileCheck %s < %t.ll\n*)\n\n"
    return run + src.rstrip("\n") + "\n\n(*\n" + "\n".join(checks) + "\n*)\n"


EXACT_STDOUT_RE = re.compile(r'EXPECT_EQ\(R\.Stdout,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')


def build_exact_stdout_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    if call_kind != "compileAndRun":
        return None
    if "ExitCode, 0)" not in remainder:
        return None
    m = EXACT_STDOUT_RE.search(remainder)
    if not m:
        return None
    stdout = decode_concatenated_strings(m.group(1))
    if stdout == "":
        raise ValueError("empty expected stdout, nothing to CHECK")
    fp = flags_prefix(flags)
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
    compile_line, exec_prefix, body = compile_lines_and_body(fp, src, stdin_text)
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
    msgs = []
    for pat in patterns:
        for m in pat.finditer(remainder):
            msgs.append(decode_concatenated_strings(m.group(1)))
    return msgs


NONZERO_EXIT_RE = re.compile(r"(?:NE\(R\.ExitCode,\s*0\)|EQ\(R\.ExitCode,\s*[1-9]\d*\))")


def build_nonzero_exit_case(suite, name, call_kind, src, flags, stdin_text, remainder):
    if call_kind != "compileAndRun":
        return None
    if not NONZERO_EXIT_RE.search(remainder):
        return None
    msgs = find_all_substring_checks(remainder, STDERR_FIND_RES)
    if not msgs:
        return None  # no message check -- flagged for manual review upstream
    fp = flags_prefix(flags)
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
        compile_line, exec_prefix, body = compile_lines_and_body(fp, src, stdin_text)
        run = (
            f"(*\n{compile_line}"
            f"RUN: not {exec_prefix} > %t.out 2> %t.err\n"
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
            f"(*\nRUN: not %plang {fp}%s -o %t 2> %t.err\n"
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
    if call_kind != "compileAndRun":
        return None
    if "ExitCode, 0)" not in remainder:
        return None
    msgs = find_all_substring_checks(remainder, STDOUT_FIND_RES)
    if not msgs:
        return None
    fp = flags_prefix(flags)
    compile_line, exec_prefix, body = compile_lines_and_body(fp, src, stdin_text)
    run = f"(*\n{compile_line}RUN: {exec_prefix} | FileCheck %s\n*)\n\n"
    checks = "\n".join(f"CHECK-DAG: {m}" for m in msgs)
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
    run = (
        f"(*\nRUN: not %plang {fp}-emit-llvm %s -o %t.ll 2> %t.err\n"
        f"RUN: FileCheck %s < %t.err\n*)\n\n"
    )
    return run + src.rstrip("\n") + f"\n\n(*\nCHECK: {msgs[0]}\n*)\n"


BUILDERS = [
    build_ir_substring_case,
    build_exact_stdout_case,
    build_nonzero_exit_case,
    build_stdout_substring_case,
    build_emit_ir_rejection_case,
]

SKIP_MARKERS = ("for (", "while (")


def main() -> int:
    check_only = "--check" in sys.argv
    text = SRC_FILE.read_text()
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
        if parsed is None:
            skipped.append((suite, name, "no compileAndRun/compileAndEmitIR call"))
            continue
        call_kind, src, flags, stdin_text, remainder = parsed

        pas = None
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

        out_dir = OUT_ROOT / suite
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
