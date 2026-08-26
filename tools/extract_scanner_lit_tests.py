#!/usr/bin/env python3
"""Extract test/Lex/scanner_test.cpp's TEST() cases into test/Lex/*.pas.

Every case in scanner_test.cpp does one thing: construct a Scanner (via
makeScanner()/makeScannerEP(), i.e. default ISO 7185 or -std=iso10206) over
a TempFile, then call Scanner::next() some number of times, asserting each
call's .Kind/.Lexeme (and sometimes locOf(...).Line/.Column, and sometimes
scanDiags' emptiness/message substring).

-dump-tokens (see lib/Frontend/Frontend.cpp) is the CLI mirror of exactly
this: it prints "<line>:<col>: KindName \"lexeme\"" per Scanner::next() call,
in order, then exits nonzero iff any diagnostic fired. So each ordinary
TEST() case becomes one .pas file whose SOURCE is the TempFile's own
content, verbatim, and whose CHECK block is one line per .next() call that
had a kind assertion -- using a throwaway [[P#:[0-9]+:[0-9]+]] capture for
the position when the original test didn't itself assert an exact
line/column (the overwhelming majority), so line-number fragility (a
repeat mistake this project's own memory already flags) never enters the
picture for the auto-converted bulk.

A handful of shapes are NOT handled by this script and are hand-converted
separately (see project memory): the one test that constructs two
independent Scanners in one TEST() (EitherTerminatorClosesEitherComment),
FileNotFound (no TempFile at all, a hardcoded nonexistent path), the 4
ScannerLocation-suite cases (need an exact pinned line:col, verified
empirically per-file rather than trusted from this script's line-counting),
and the loop-based table-driven cases (EPKeywordsRecognized,
EPKeywordsAreIdentifiersIn7185, ScaleFactorSignAndCase already fits the
plain slot model fine and IS auto-converted; ComplexStarCommentSequence
also fits and IS auto-converted -- only the two "fresh scanner per
iteration" cases, EPKeywordsRecognized/EPKeywordsAreIdentifiersIn7185,
are hand-converted, since they need concatenating N independent sources
into one, which this script does not attempt).
"""
from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_FILE = os.path.join(REPO_ROOT, "test", "Lex", "scanner_test.cpp")
OUT_ROOT = os.path.join(REPO_ROOT, "test", "Lex")

# Cases this script deliberately does not attempt -- hand-converted instead.
SKIP = {
    ("ScannerLexicalAlternatives", "EitherTerminatorClosesEitherComment"),
    ("ScannerErrors", "FileNotFound"),
    ("ScannerLocation", "FirstTokenColumn"),
    ("ScannerLocation", "MultilineTracking"),
    ("ScannerLocation", "ColumnAfterComment"),
    ("ScannerComments", "MultilineBraceComment"),  # asserts locOf(T).Line
    ("ScannerEP", "EPKeywordsRecognized"),            # fresh scanner per iter
    ("ScannerEP", "EPKeywordsAreIdentifiersIn7185"),   # fresh scanner per iter
    # These two loop over a braced initializer-list, re-using one `Token T`/
    # one EXPECT_EQ pair textually once per body -- the static slot-model
    # below would only see ONE loop iteration's worth of text, not N, and
    # for ScaleFactorSignAndCase the lexeme comes from the loop variable
    # (not a string literal) at that. Silently wrong if auto-converted;
    # hand-converted instead.
    ("ScannerLiterals", "ScaleFactorSignAndCase"),
    ("ScannerComments", "ComplexStarCommentSequence"),
    # -dump-tokens' own dump loop stops at the FIRST Eof (the only sensible
    # behavior for the flag itself -- no real caller wants infinite Eof
    # spam), which structurally cannot observe "calling next() again after
    # Eof still returns Eof" -- a pure Scanner-API safety property with no
    # Pascal-source-observable difference and no real caller that would ever
    # call next() again after seeing Eof. No CLI equivalent; dropped, not
    # forced.
    ("ScannerEof", "RepeatedEofCalls"),
    # Loops over a struct array using E.Kind/E.Lexeme (member access on the
    # loop variable) -- the _KINDOF/_LEXOF regexes only match TokenKind::X
    # literals and quoted-string literals, not arbitrary member access, so
    # this produces zero slots-with-checks if auto-converted (caught by the
    # zero-check-lines guard in main(), not silently). Also near-fully
    # redundant with test/Lex/Smoke/dump-tokens-prints-a-token-per-line.pas
    # (Phase A), which already exercises this exact "whole small program"
    # token-stream shape -- dropped rather than hand-converted.
    ("ScannerIntegration", "SmallProgram"),
}


def kebab(name: str) -> str:
    # Acronym-aware: "EPKeywordsRecognized" -> "ep-keywords-recognized", not
    # "e-p-keywords-recognized" -- the plain per-capital-letter split the
    # rest of this migration's other extraction scripts use is wrong once a
    # name contains a run of capitals (EP, ISO7185, ...). First pass splits
    # an acronym from the CamelCase word that follows it; second pass
    # handles every ordinary lower-to-upper transition.
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1-\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", s)
    return s.lower()


def find_matching_brace(text: str, open_pos: int) -> int:
    """open_pos indexes the '{' itself; returns the index of its match."""
    depth = 0
    i = open_pos
    in_str = False
    in_char = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
        else:
            if c == "/" and i + 1 < len(text) and text[i + 1] == "/":
                # Line comment -- skip to (not past) the newline.  Needed
                # because this test file's comments are full of English
                # contractions ("parser's", "wasn't") whose apostrophe would
                # otherwise be mistaken for the start of a C++ char literal,
                # desyncing the brace count until some LATER quote closes it.
                nl = text.find("\n", i)
                i = nl if nl != -1 else len(text)
                continue
            if c == '"':
                in_str = True
            elif c == "'":
                in_char = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    raise ValueError("unbalanced braces")


TEST_RE = re.compile(r"TEST\((\w+),\s*(\w+)\)\s*\{")


def split_tests(text: str):
    """Yield (suite, name, body) for every TEST(Suite, Name) { ... } block."""
    for m in TEST_RE.finditer(text):
        open_brace = text.index("{", m.end() - 1)
        close_brace = find_matching_brace(text, open_brace)
        yield m.group(1), m.group(2), text[open_brace + 1:close_brace]


def decode_cpp_string_concat(fragment: str) -> str:
    """Decode one or more adjacent C++ string literals ("a" "b") into text."""
    out = []
    i = 0
    while i < len(fragment):
        if fragment[i].isspace():
            i += 1
            continue
        if fragment[i] != '"':
            break
        i += 1
        while i < len(fragment) and fragment[i] != '"':
            if fragment[i] == "\\":
                nxt = fragment[i + 1]
                out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"',
                            "'": "'", "0": "\0"}.get(nxt, nxt))
                i += 2
            else:
                out.append(fragment[i])
                i += 1
        i += 1  # closing quote
    return "".join(out)


def extract_tempfile_content(body: str) -> tuple[str | None, int]:
    """Find `TempFile F(<string-literal(s)>)`; return (decoded, end_index)."""
    m = re.search(r"TempFile\s+F\(", body)
    if not m:
        return None, -1
    start = m.end()
    # Walk forward collecting the concatenated string-literal argument,
    # respecting escapes, until the closing ')'.
    i = start
    depth = 1
    frag_start = i
    while i < len(body):
        c = body[i]
        if c == '"':
            i += 1
            while i < len(body) and body[i] != '"':
                if body[i] == "\\":
                    i += 2
                else:
                    i += 1
            i += 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    fragment = body[frag_start:i]
    return decode_cpp_string_concat(fragment), i


@dataclass
class Slot:
    kind: str | None = None
    lexeme: str | None = None


def unescape_cpp(s: str) -> str:
    return decode_cpp_string_concat('"' + s + '"')


# One alternative per shape, each using ONLY named groups (no bare numbered
# groups) so there is no numbering arithmetic to get wrong when alternatives
# are added/reordered.
_CAPTURE  = r"(?P<capture>Token\s+(?P<capname>\w+)\s*=\s*S\.next\(\);)"
_DISCARD1 = r"(?P<discard1>\(void\)S\.next\(\);)"
_INLINE   = r"(?P<inlinekind>EXPECT_EQ\(S\.next\(\)\.Kind,\s*TokenKind::(?P<inlinekindval>\w+)\))"
_DISCARD2 = r"(?P<discard2>(?<!\.)\bS\.next\(\);)"
_KINDOF   = r"(?P<kindof>EXPECT_EQ\((?P<kindofname>\w+)\.Kind,\s*TokenKind::(?P<kindofval>\w+)\))"
_LEXOF    = r'(?P<lexof>EXPECT_EQ\((?P<lexofname>\w+)\.Lexeme,\s*"(?P<lexofval>(?:[^"\\]|\\.)*)"\))'

SLOT_SCAN_RE = re.compile(
    "|".join([_CAPTURE, _DISCARD1, _INLINE, _DISCARD2, _KINDOF, _LEXOF]))


def parse_slots(body: str):
    """Return an ordered list[Slot] by replaying every S.next() call site."""
    slots: list[Slot] = []
    name_to_slot: dict[str, int] = {}

    for m in SLOT_SCAN_RE.finditer(body):
        if m.group("capture"):
            slots.append(Slot())
            name_to_slot[m.group("capname")] = len(slots) - 1
        elif m.group("discard1") or m.group("discard2"):
            slots.append(Slot())
        elif m.group("inlinekind"):
            slots.append(Slot(kind=m.group("inlinekindval")))
        elif m.group("kindof"):
            name = m.group("kindofname")
            if name in name_to_slot:
                slots[name_to_slot[name]].kind = m.group("kindofval")
        elif m.group("lexof"):
            name = m.group("lexofname")
            if name in name_to_slot:
                slots[name_to_slot[name]].lexeme = unescape_cpp(m.group("lexofval"))
    return slots


def diag_substring(body: str) -> str | None:
    m = re.search(r'scanDiags\[0\]\.Message\.find\("((?:[^"\\]|\\.)*)"\)', body)
    return unescape_cpp(m.group(1)) if m else None


PLANG_BIN = os.path.join(REPO_ROOT, "build", "bin", "plang")

TOKEN_LINE_RE = re.compile(r'^\d+:\d+: (\w+)(?: "((?:[^"\\]|\\.)*)")?$')


def probe(content: str, dialect_flag: str) -> tuple[int, list[tuple[str, str]], str]:
    """Run the real plang -dump-tokens against content.

    Ground truth, not a static guess: some sources (e.g. an underscored
    identifier under the default dialect) make the Scanner emit an
    Error-severity diagnostic while STILL handing back a usable token --
    deliberate error recovery, which the original scanner_test.cpp checks
    at the API level without necessarily checking scanDiags at all. Only
    a real run tells you whether THIS source is one of those; a static
    "did the C++ body check scanDiags.empty()" heuristic cannot.
    """
    import subprocess
    import tempfile
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pas", delete=False) as f:
        f.write(content)
        path = f.name
    try:
        args = [PLANG_BIN, "-dump-tokens"]
        if dialect_flag:
            args.append(dialect_flag.strip())
        args.append(path)
        r = subprocess.run(args, capture_output=True, text=True, timeout=10)
        tokens = []
        for line in r.stdout.splitlines():
            tm = TOKEN_LINE_RE.match(line)
            if tm:
                tokens.append((tm.group(1), tm.group(2) or ""))
        return r.returncode, tokens, r.stderr
    finally:
        os.remove(path)


def build_pas(suite: str, name: str, body: str) -> str | None:
    content, _ = extract_tempfile_content(body)
    if content is None:
        return None
    is_ep = "makeScannerEP(" in body
    dialect_flag = " -std=iso10206" if is_ep else ""

    slots = parse_slots(body)
    substr = diag_substring(body)

    if substr is not None:
        # Explicit diagnostic-message-substring case: verified against real
        # stderr, not just assumed to fail -- confirms the message text is
        # what the .pas file will actually see, not what the C++ source
        # merely claims.
        rc, _tokens, stderr = probe(content, dialect_flag)
        if rc == 0 or substr not in stderr:
            return None
        out = ["(*",
               f"RUN: not %plang_ir -dump-tokens{dialect_flag} %s 2> %t.err",
               "RUN: FileCheck --check-prefix=ERR %s < %t.err",
               "*)", "", content.rstrip("\n"), "",
               "(*", f"ERR: {substr}", "*)", ""]
        return "\n".join(out)

    rc, real_tokens, _stderr = probe(content, dialect_flag)

    check_lines = []
    n = 0
    prev_had_check = False
    real_idx = 0
    for slot in slots:
        if slot.kind is None:
            prev_had_check = False
            real_idx += 1
            continue
        # Cross-check against the REAL output at the same position -- if
        # this script's static extraction got the kind or lexeme wrong
        # (script bug, not a source-text bug), fail loud here rather than
        # silently emitting a CHECK line the real binary would never
        # satisfy in the first place.
        if real_idx >= len(real_tokens):
            return None
        real_kind, real_lexeme = real_tokens[real_idx]
        if real_kind != slot.kind:
            return None
        if slot.lexeme is not None and real_lexeme != slot.lexeme:
            return None
        real_idx += 1
        n += 1
        prefix = "CHECK" if not prev_had_check else "CHECK-NEXT"
        # CHECK is a prefix/substring match, not a full-line match (no
        # --match-full-lines anywhere in this suite) -- when the original
        # test never asserted a lexeme, omit it from the pattern rather than
        # inventing an empty one: a bare `Div` still matches the real
        # `12:1: Div "div"` line, faithfully translating "kind only, lexeme
        # unchecked" instead of asserting a lexeme value (empty) the real
        # output would never actually have.
        if slot.lexeme is not None:
            check_lines.append(
                f'{prefix}: [[P{n}:[0-9]+:[0-9]+]]: {slot.kind} "{slot.lexeme}"')
        else:
            check_lines.append(
                f'{prefix}: [[P{n}:[0-9]+:[0-9]+]]: {slot.kind}')
        prev_had_check = True

    fails = rc != 0

    # Zero check_lines is only legitimate for a bare "must fail, nothing
    # else asserted" case (fails=True routes to a plain `not ... %s` RUN
    # line below, no FileCheck at all). Any other zero-check_lines case
    # means every slot's kind went unresolved -- almost always a loop shape
    # this script's regexes don't recognize (E.Kind/E.Lexeme member access,
    # not a TokenKind:: literal or quoted string) rather than a genuinely
    # check-nothing test. Fail loud here instead of emitting a RUN line
    # whose FileCheck invocation has no CHECK directives to match against
    # (which real lit would catch too, just later and less legibly).
    if not check_lines and not fails:
        return None

    run_lines = []
    if fails and not check_lines:
        run_lines.append(f"RUN: not %plang_ir -dump-tokens{dialect_flag} %s")
    elif fails and check_lines:
        run_lines.append(f"RUN: not %plang_ir -dump-tokens{dialect_flag} %s | FileCheck %s")
    else:
        run_lines.append(f"RUN: %plang_ir -dump-tokens{dialect_flag} %s | FileCheck %s")

    out = ["(*"]
    out.extend(run_lines)
    out.append("*)")
    out.append("")
    out.append(content.rstrip("\n"))
    out.append("")
    if check_lines:
        out.append("(*")
        out.extend(check_lines)
        out.append("*)")
    out.append("")
    return "\n".join(out)


def main():
    text = open(SRC_FILE).read()
    converted = 0
    skipped = 0
    failed = []
    for suite, name, body in split_tests(text):
        if (suite, name) in SKIP:
            skipped += 1
            continue
        pas = build_pas(suite, name, body)
        if pas is None:
            failed.append((suite, name))
            continue
        out_dir = os.path.join(OUT_ROOT, suite)
        os.makedirs(out_dir, exist_ok=True)
        out_path = os.path.join(out_dir, kebab(name) + ".pas")
        with open(out_path, "w") as f:
            f.write(pas)
        converted += 1
    print(f"converted={converted} skipped={skipped} failed={len(failed)}")
    if failed:
        for s, n in failed:
            print(f"  FAILED: {s}.{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
