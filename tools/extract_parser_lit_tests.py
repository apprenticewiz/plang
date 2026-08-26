#!/usr/bin/env python3
"""Extract test/Parse/parser_test.cpp's TEST() cases into test-lit/Parse/*.pas.

Every case in parser_test.cpp does one of three things:

  1. Calls parse(Src[, Opts]) and inspects the returned AST's structure via
     llvm::dyn_cast + field access (the overwhelming majority). -dump-parse-tree
     prints the exact same tree via printAst() (lib/AST/AstPrinter.cpp) that
     Sema never touches (it only annotates nodes in place, confirmed by grep
     for ImplicitCast/wrapIn patterns -- zero hits in lib/Sema/*.cpp), so the
     real, empirically-captured stdout of `plang -dump-parse-tree` on the same
     source is a strictly *stronger* translation of these assertions than
     re-deriving individual substrings from the C++ field checks would be: it
     proves the exact tree shape (operator precedence, nesting, associativity)
     that a bag of independent substring checks cannot. Converted via the
     "Exit-0 + exact-stdout" idiom (FileCheck --strict-whitespace
     --match-full-lines, CHECK + CHECK-NEXT chained through the whole dump).

  2. Calls astOf(Src[, Opts]) (the AstPrinter suite) and checks
     S.find("substr") == / != std::string::npos -- these ARE the intended
     substring assertions, so they're extracted as literal CHECK/CHECK-NOT
     substring patterns (no --match-full-lines), same as the original test.

  3. Calls parse(Src) and checks it's nullptr (rejected) or non-null with no
     further structure check (ParserErrors suite; the ONE accept-only case,
     MissingFunctionReturnTypeIsAcceptedForSemaToJudge, is why -dump-parse-tree
     exists at all -- -dump-ast would collapse this into the same failure as
     an actual parse error).

Everything is verified against the real plang binary before being trusted
(ground truth, not a static guess from the C++ source) -- see probe().

A handful of shapes are NOT handled by this script and are hand-converted
separately (see project memory): the 3 loop-parametrized table-driven cases
(AllRelops/AllAddops/AllMulops, whose source is built at runtime via
std::string concatenation, not a literal argument) and
DeeplyNestedParensReportOneDiagnosticNotACrash (source built via std::string
repetition, and asserts specific diagnostic-message/-count properties this
script's plain accept/reject/structural buckets don't model).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_FILE = os.path.join(REPO_ROOT, "test", "Parse", "parser_test.cpp")
OUT_ROOT = os.path.join(REPO_ROOT, "test-lit", "Parse")
PLANG_BIN = os.path.join(REPO_ROOT, "build", "bin", "plang")

SKIP = {
    ("ParserExpressions", "AllRelops"),
    ("ParserExpressions", "AllAddops"),
    ("ParserExpressions", "AllMulops"),
    ("ParserErrors", "DeeplyNestedParensReportOneDiagnosticNotACrash"),
}


def kebab(name: str) -> str:
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1-\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", s)
    return s.lower()


def find_matching_brace(text: str, open_pos: int) -> int:
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
    for m in TEST_RE.finditer(text):
        open_brace = text.index("{", m.end() - 1)
        close_brace = find_matching_brace(text, open_brace)
        yield m.group(1), m.group(2), text[open_brace + 1:close_brace]


def decode_cpp_string_concat(fragment: str) -> str:
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


def unescape_cpp(s: str) -> str:
    return decode_cpp_string_concat('"' + s + '"')


def extract_call_arg_and_ep(body: str, call_name: str):
    """Find `call_name(<string-literal(s)>[, epOpts()])`.

    Returns (decoded_source, is_ep) or (None, False) if the first argument
    isn't a plain string literal (e.g. a local variable built at runtime --
    the loop-parametrized / DeeplyNestedParens cases this script skips).
    """
    m = re.search(rf"\b{call_name}\(", body)
    if not m:
        return None, False
    i = m.end()
    depth = 1
    start = i
    comma_pos = None
    while i < len(body):
        c = body[i]
        if c == '"':
            i += 1
            while i < len(body) and body[i] != '"':
                i += 2 if body[i] == "\\" else 1
            i += 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                break
        elif c == "," and depth == 1 and comma_pos is None:
            comma_pos = i
        i += 1
    end = i
    arg1_end = comma_pos if comma_pos is not None else end
    arg1 = body[start:arg1_end].strip()
    if not arg1.startswith('"'):
        return None, False
    content = decode_cpp_string_concat(arg1)
    rest = body[arg1_end:end] if comma_pos is not None else ""
    return content, "epOpts()" in rest


def probe(content: str, dialect_flag: str) -> tuple[int, list[str], str]:
    """Run the real plang -dump-parse-tree against content. Ground truth."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pas", delete=False) as f:
        f.write(content)
        path = f.name
    try:
        args = [PLANG_BIN, "-dump-parse-tree"]
        if dialect_flag:
            args.append(dialect_flag.strip())
        args.append(path)
        r = subprocess.run(args, capture_output=True, text=True, timeout=10)
        return r.returncode, r.stdout.splitlines(), r.stderr
    finally:
        os.remove(path)


_PRESENT = r'(?P<present>EXPECT_NE\(S\.find\("(?P<pval>(?:[^"\\]|\\.)*)"\),\s*std::string::npos\))'
_ABSENT = r'(?P<absent>EXPECT_EQ\(S\.find\("(?P<aval>(?:[^"\\]|\\.)*)"\),\s*std::string::npos\))'
FIND_RE = re.compile("|".join([_PRESENT, _ABSENT]))


def build_astprinter(name: str, body: str) -> str | None:
    content, is_ep = extract_call_arg_and_ep(body, "astOf")
    if content is None:
        return None
    dialect_flag = " -std=iso10206" if is_ep else ""
    rc, out_lines, _stderr = probe(content, dialect_flag)
    if rc != 0:
        return None
    out_text = "\n".join(out_lines)

    checks = []
    for m in FIND_RE.finditer(body):
        if m.group("present"):
            substr = unescape_cpp(m.group("pval"))
            if substr not in out_text:
                return None
            checks.append(("CHECK", substr))
        else:
            substr = unescape_cpp(m.group("aval"))
            if substr in out_text:
                return None
            checks.append(("CHECK-NOT", substr))
    if not checks:
        return None

    lines = ["(*",
             f"RUN: %plang_ir -dump-parse-tree{dialect_flag} %s | FileCheck %s",
             "*)", "", content.rstrip("\n"), "", "(*"]
    for prefix, substr in checks:
        lines.append(f"{prefix}: {substr}")
    lines.append("*)")
    lines.append("")
    return "\n".join(lines)


def build_reject(name: str, body: str) -> str | None:
    content, is_ep = extract_call_arg_and_ep(body, "parse")
    if content is None:
        return None
    dialect_flag = " -std=iso10206" if is_ep else ""
    rc, _out_lines, _stderr = probe(content, dialect_flag)
    if rc == 0:
        return None
    return "\n".join(["(*",
                       f"RUN: not %plang_ir -dump-parse-tree{dialect_flag} %s",
                       "*)", "", content.rstrip("\n"), ""])


def build_accept_bare(name: str, body: str) -> str | None:
    content, is_ep = extract_call_arg_and_ep(body, "parse")
    if content is None:
        return None
    dialect_flag = " -std=iso10206" if is_ep else ""
    rc, _out_lines, _stderr = probe(content, dialect_flag)
    if rc != 0:
        return None
    return "\n".join(["(*",
                       f"RUN: %plang_ir -dump-parse-tree{dialect_flag} %s",
                       "*)", "", content.rstrip("\n"), ""])


def build_structural(name: str, body: str) -> str | None:
    content, is_ep = extract_call_arg_and_ep(body, "parse")
    if content is None:
        return None
    dialect_flag = " -std=iso10206" if is_ep else ""
    rc, out_lines, _stderr = probe(content, dialect_flag)
    if rc != 0 or not out_lines:
        return None
    lines = ["(*",
             f"RUN: %plang_ir -dump-parse-tree{dialect_flag} %s | "
             "FileCheck --strict-whitespace --match-full-lines %s",
             "*)", "", content.rstrip("\n"), "", "(*"]
    for i, out_line in enumerate(out_lines):
        prefix = "CHECK" if i == 0 else "CHECK-NEXT"
        # No space after the colon: --strict-whitespace disables the usual
        # single-space separator skip between "CHECK:" and the pattern, so
        # a literal space here would require a leading space in the real
        # output line that isn't actually there (confirmed empirically --
        # matches this project's own established convention elsewhere,
        # e.g. test-lit/CodeGen's exact-stdout idiom).
        lines.append(f"{prefix}:{out_line}")
    lines.append("*)")
    lines.append("")
    return "\n".join(lines)


def main():
    text = open(SRC_FILE).read()
    converted = 0
    skipped = 0
    failed = []
    for suite, name, body in split_tests(text):
        if (suite, name) in SKIP:
            skipped += 1
            continue

        if suite == "AstPrinter":
            pas = build_astprinter(name, body)
        elif suite == "ParserErrors":
            if "EXPECT_EQ(parse(" in body:
                pas = build_reject(name, body)
            elif "EXPECT_NE(parse(" in body:
                pas = build_accept_bare(name, body)
            else:
                pas = None
        else:
            pas = build_structural(name, body)

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
