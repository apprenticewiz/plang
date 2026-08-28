#!/usr/bin/env python3
"""lint_test.py -- standing hygiene checks over test/**/*.pas.

Manifest-free (see project memory: verify_lit_migration.py's original
count/pass-parity-against-a-manifest design was never built, since no
manifest was ever produced across the whole GTest->lit migration -- ad hoc
case-count cross-checks + full CI green did the job instead). This tool
checks three things instead, each with a documented repeat-offense history
across this migration's own commits:

  1. A bare '}' inside a (* *) directive block closes the comment early
     (ISO 7185 Sec6.1.8: "either terminator ends either"), silently turning
     the rest of the directive text into garbage Pascal tokens. Hit at
     least 4 times (PRs #39, #40, #42, and the Phase F storage_test.cpp
     migration) -- the last one caught only by hand-reading a
     -dump-tokens dump, not by any existing check.
  2. A bare '%t' (or %t.suffix) as the executed command in a RUN: pipeline
     segment, instead of '%run %t', silently defeats
     PLANG_TEST_RUN_WRAPPER (the guardheap allocator) for that one file.
     Hit at scale once already: 282 of 364 CodeGen files violated this
     until caught in PR #38.

Both checks are precise, not heuristic-by-vocabulary: check 1 is scoped to
exactly the regions of a file that will actually reach the Scanner (the
whole file, unless it uses split-file, in which case only chunks with a
.pas name -- the split-file preamble and non-.pas chunks, e.g. a synthetic
.po fixture, are provably never scanned as Pascal, confirmed empirically
in project memory). Check 2 parses RUN: line pipeline structure rather
than grepping for the substring, so '%plang %s -o %t' (a %t OUTPUT
argument, harmless) is not confused with '%t | FileCheck ...' (a %t
EXECUTED, the real hazard).

Exit code is nonzero iff --strict is passed and any finding exists;
otherwise this always exits 0 and just reports (see --strict below).
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_ROOT = os.path.join(REPO_ROOT, "test")

# Files whose whole point is to exercise the ISO 7185 Sec6.1.8
# either-terminator-closes-either rule directly -- check 1 firing on these
# is it working correctly, not a hazard to fix. Documented here, not
# silently swallowed, so a real new hazard in either file still surfaces.
BRACE_CHECK_EXEMPT = {
    "test/Lex/ScannerLexicalAlternatives/either-terminator-closes-either-comment.pas",
    "test/Conformance/Error/prt1622.pas",
    # -std=turbo has the OPPOSITE rule (Scanner.cpp's skipCommentTurbo): a
    # comment must be closed by its own kind, and mixing them is
    # err_comment_delim_mismatch, not an accepted alternative. This file's
    # two split-file chunks deliberately mix them to exercise that error,
    # which is exactly what this check -- written for the ISO rule, with no
    # dialect awareness -- flags as a hazard. Working as intended, not a
    # hazard to fix, same as the two entries above for the opposite rule.
    "test/Lex/ScannerTurbo/mismatched-comment-delimiter-is-an-error-under-turbo.pas",
    # Same -std=turbo rule, the other side of it: a '*)' embedded inside a
    # brace comment that DOES go on to close correctly (a real '}' follows
    # it) is inert under Turbo -- just comment text, not a terminator, so
    # the whole thing compiles clean. This check's ISO-only simulation
    # instead treats that embedded '*)' as closing the comment right there,
    # which is exactly the (Turbo-only) case this file exists to prove
    # doesn't happen. Working as intended, not a hazard to fix.
    "test/Lex/ScannerTurbo/embedded-wrong-kind-closer-is-inert-when-comment-closes-correctly.pas",
    # A directive is still a comment syntactically (skipDirective in
    # lib/Lex/Directives.cpp), so under -std=turbo it closes by the same
    # matched-delimiter rule an ordinary Turbo comment does. This file's two
    # split-file chunks deliberately mix '{'/'*)' and '(*'/'}' to exercise
    # that error on the directive path specifically (not just the plain
    # comment path the two entries above already cover) -- working as
    # intended, not a hazard to fix.
    "test/Lex/ScannerTurbo/mismatched-directive-delimiter-is-an-error-too.pas",
    # These three files' own header comments (a leading '(* ... *)' block,
    # this project's usual per-file explanation) describe '{$...}' directive
    # syntax IN ENGLISH PROSE, e.g. "a `{$name}` naming no category..." --
    # literal '{'/'}' characters as text, not real nested comment syntax.
    # This check has no dialect awareness and simulates the header comment
    # under ISO 7185's either-terminator-closes-either rule, where a bare
    # '}' inside the prose would end the '(* *)' header early -- a real
    # hazard for a file ISO/EP might compile, but these three only ever
    # compile under -std=turbo (see each file's own RUN: line), where the
    # matched-delimiter rule this check doesn't model means the '(* *)'
    # header safely contains its own literal braces. Not a hazard to fix.
    "test/Driver/Turbo/an-unrecognized-directive-is-a-clear-diagnostic-not-silence.pas",
    "test/Driver/Turbo/message-directives-are-informational-and-still-compile.pas",
    "test/Driver/Turbo/warning-directives-warn-and-still-compile.pas",
    # Same reasoning as the three entries just above (only ever compiles
    # under -std=turbo -- see its own RUN: line -- so the matched-delimiter
    # rule this check doesn't model is the one that actually applies), for
    # a fourth file added when the I/INCLUDE directive did: its header
    # prose literally discusses '{$I+}'/'{$I-}' syntax, the very two
    # spellings dispatchIncludeDirective must NOT treat as an include (see
    # CompilerSwitches.def's 'i' == IOChecks).  Not a hazard to fix.
    "test/Driver/Turbo/dollar-i-plus-and-minus-are-not-mistaken-for-an-include.pas",
    # Same reasoning again (only ever compiles under -std=turbo -- see each
    # file's own RUN: line -- so the matched-delimiter rule this check
    # doesn't model is the one that actually applies), for the {$R+}-style
    # switch and accept-and-ignore directive tests: their header prose and/or
    # CHECK blocks discuss '{$R+}'/'{$R-}' syntax and 'assert'/switch
    # examples in English, the same shape as the entries just above.  Not a
    # hazard to fix.
    "test/Driver/Turbo/accept-and-ignore-directives-warn-but-still-compile-and-run.pas",
    "test/Driver/Turbo/assert-with-a-message-aborts-with-runtime-error-227.pas",
    "test/Driver/Turbo/assert-with-assertions-off-compiles-to-nothing.pas",
    "test/Driver/Turbo/objectchecks-and-goto-have-no-letter-spelling-of-their-own.pas",
    "test/Driver/Turbo/switch-directive-long-name-form-works-the-same-as-the-letter.pas",
    "test/Driver/Turbo/switch-directive-r-minus-turns-range-checks-back-off.pas",
    "test/Driver/Turbo/switch-directive-r-plus-turns-range-checks-on-partway-through-the-file.pas",
    "test/Lex/ScannerTurbo/switch-directives-every-spelling-scans-cleanly.pas",
}


# ---------------------------------------------------------------------------
# Shared: split-file region detection
# ---------------------------------------------------------------------------

CHUNK_MARKER_RE = re.compile(r"^//---\s*(\S+)\s*$", re.M)


def compiled_pas_regions(text: str) -> list[tuple[int, int]]:
    """Byte-offset (start, end) spans of `text` that will actually reach
    the Scanner as Pascal source.

    No split-file markers: the whole file. With markers: only chunks whose
    name ends in .pas (the split-file preamble, and any non-.pas chunk
    such as a synthetic .po/.txt fixture, are never compiled).
    """
    markers = list(CHUNK_MARKER_RE.finditer(text))
    if not markers:
        return [(0, len(text))]
    regions = []
    for i, m in enumerate(markers):
        name = m.group(1)
        start = m.end() + 1 if m.end() < len(text) and text[m.end()] == "\n" else m.end()
        end = markers[i + 1].start() if i + 1 < len(markers) else len(text)
        if name.endswith(".pas"):
            regions.append((start, end))
    return regions


# ---------------------------------------------------------------------------
# Check 1: a bare '}' (or, symmetrically, a bare '*)') closing a directive
# comment early, inside a region that actually reaches the Scanner.
# ---------------------------------------------------------------------------

def find_early_comment_closes(text: str, regions: list[tuple[int, int]]):
    """Yield (line, col, kind) for each hazard found.

    Walks each region char-by-char, tracking Pascal single-quoted string
    literals (the only construct that can hide a brace from meaning
    "comment" in real Pascal) and (* *) / { } comment nesting per the ISO
    7185 Sec6.1.8 "either terminator ends either" rule -- the same rule
    plang's own Scanner.cpp implements. A finding is a (* *)-opened comment
    that a bare '}' closes before the '*)' the author placed at the end of
    the intended block is ever reached, or symmetrically a { }-opened
    comment closed early by a stray '*)'.
    """
    findings = []
    for start, end in regions:
        i = start
        in_str = False
        # comment_kind: None (not in comment), '(*' or '{' (which opener)
        comment_kind = None
        comment_open_pos = None
        while i < end:
            c = text[i]
            if in_str:
                if c == "'":
                    in_str = False
                i += 1
                continue
            if comment_kind is not None:
                if c == "}" :
                    if comment_kind == "{":
                        comment_kind = None  # correctly closed, no finding
                    else:
                        # (* ... }  -- closes a (* *) block early.
                        findings.append((comment_open_pos, i, "brace-closes-paren-comment"))
                        comment_kind = None
                    i += 1
                    continue
                if c == "*" and i + 1 < end and text[i + 1] == ")":
                    if comment_kind == "(*":
                        comment_kind = None
                    else:
                        # { ... *)  -- closes a { } block early.
                        findings.append((comment_open_pos, i, "star-paren-closes-brace-comment"))
                        comment_kind = None
                    i += 2 if comment_kind is None else 1
                    continue
                i += 1
                continue
            # Not in a comment or string.
            if c == "'":
                in_str = True
                i += 1
                continue
            if c == "(" and i + 1 < end and text[i + 1] == "*":
                comment_kind = "(*"
                comment_open_pos = i
                i += 2
                continue
            if c == "{":
                comment_kind = "{"
                comment_open_pos = i
                i += 1
                continue
            i += 1
    return findings


def offset_to_line_col(text: str, offset: int) -> tuple[int, int]:
    line = text.count("\n", 0, offset) + 1
    last_nl = text.rfind("\n", 0, offset)
    col = offset - last_nl
    return line, col


# ---------------------------------------------------------------------------
# Check 2: bare %t (or %t.suffix) as the EXECUTED command in a RUN: line,
# instead of %run %t.
# ---------------------------------------------------------------------------

RUN_LINE_RE = re.compile(r"^\s*RUN:\s*(.*)$", re.M)
BARE_T_EXEC_RE = re.compile(r"^%t(\.[A-Za-z0-9_]+)?$")


def split_pipeline_segments(run_cmd: str) -> list[str]:
    # Good enough for this corpus's RUN: lines: split on the shell
    # separators lit's own RUN: lines actually use, outside of quotes.
    segments = []
    cur = []
    i = 0
    in_quote = None
    while i < len(run_cmd):
        c = run_cmd[i]
        if in_quote:
            cur.append(c)
            if c == in_quote:
                in_quote = None
            i += 1
            continue
        if c in "\"'":
            in_quote = c
            cur.append(c)
            i += 1
            continue
        if run_cmd[i:i + 2] == "&&":
            segments.append("".join(cur))
            cur = []
            i += 2
            continue
        if c in "|;":
            segments.append("".join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    segments.append("".join(cur))
    return [s.strip() for s in segments if s.strip()]


def find_bare_t_exec(text: str):
    findings = []
    for m in RUN_LINE_RE.finditer(text):
        for seg in split_pipeline_segments(m.group(1)):
            tokens = seg.split()
            idx = 0
            # Skip prefixes that don't change what's actually executed.
            while idx < len(tokens) and tokens[idx] in ("not", "env"):
                if tokens[idx] == "env":
                    idx += 1
                    while idx < len(tokens) and "=" in tokens[idx]:
                        idx += 1
                    continue
                idx += 1
            if idx >= len(tokens):
                continue
            if BARE_T_EXEC_RE.match(tokens[idx]):
                line, _ = offset_to_line_col(text, m.start())
                findings.append((line, tokens[idx]))
    return findings


# ---------------------------------------------------------------------------
# Check 3: RUN:/CHECK:-shaped keyword-colon substrings appearing mid-line
# (not at the start of a line), which lit's own directive scanner never
# recognizes as a real directive -- almost always either dead/no-op prose
# a reader could mistake for a real check, or a directive that was meant
# to be real and is silently not running at all.
# ---------------------------------------------------------------------------

DIRECTIVE_WORD = r"(?:RUN|CHECK(?:-NOT|-DAG|-NEXT|-SAME|-EMPTY|-LABEL)?|XFAIL|REQUIRES|UNSUPPORTED|DEFINE|REDEFINE)"
DIRECTIVE_WORD_RE = re.compile(rf"{DIRECTIVE_WORD}:")
LINE_START_DIRECTIVE_RE = re.compile(rf"^\s*{DIRECTIVE_WORD}:")


def find_midline_directive_words(text: str):
    findings = []
    for lineno, line in enumerate(text.split("\n"), start=1):
        if LINE_START_DIRECTIVE_RE.match(line):
            continue
        for m in DIRECTIVE_WORD_RE.finditer(line):
            findings.append((lineno, m.group(0)))
    return findings


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def lint_file(path: str, checks: set[str]):
    text = open(path, encoding="utf-8").read()
    rel = os.path.relpath(path, REPO_ROOT)
    out = []

    if "brace" in checks and rel not in BRACE_CHECK_EXEMPT:
        regions = compiled_pas_regions(text)
        for start, _end, kind in find_early_comment_closes(text, regions):
            line, col = offset_to_line_col(text, start)
            out.append(f"{rel}:{line}:{col}: {kind}")

    if "runwrapper" in checks:
        for line, tok in find_bare_t_exec(text):
            out.append(f"{rel}:{line}: bare '{tok}' executed without %run (defeats PLANG_TEST_RUN_WRAPPER)")

    if "midline" in checks:
        for line, word in find_midline_directive_words(text):
            out.append(f"{rel}:{line}: mid-line '{word}' (not a real lit directive, possibly meant to be one)")

    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--strict", action="store_true",
                     help="exit nonzero if any finding exists")
    ap.add_argument("--only", choices=["brace", "runwrapper", "midline"],
                     action="append",
                     help="run only this check (repeatable); default: all")
    ap.add_argument("paths", nargs="*", default=[TEST_ROOT])
    args = ap.parse_args()

    checks = set(args.only) if args.only else {"brace", "runwrapper", "midline"}

    files = []
    for p in args.paths:
        if os.path.isdir(p):
            files.extend(sorted(glob.glob(os.path.join(p, "**", "*.pas"), recursive=True)))
        else:
            files.append(p)

    all_findings = []
    for f in files:
        all_findings.extend(lint_file(f, checks))

    for line in all_findings:
        print(line)
    print(f"\n{len(all_findings)} finding(s) across {len(files)} file(s), checks={sorted(checks)}")

    if args.strict and all_findings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
