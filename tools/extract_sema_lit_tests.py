#!/usr/bin/env python3
"""Extract test/Sema/sema_test.cpp's TEST() cases into test-lit/Sema/*.pas.

Every case in sema_test.cpp calls check(Src[, Opts]) (test/Sema/TestHelper.h),
which runs Scanner+Parser+Sema in-process and returns a SemaResult with:
  - .Ok            (bool, == !Sema::hasErrors())
  - .hasError(sub) (an Error-severity diagnostic's Message contains sub)
  - .hasWarning(sub)

-dump-ast (see lib/Frontend/Frontend.cpp) runs the identical pipeline and its
exit code is PROVEN to be exactly `Ok ? 0 : 1` (read directly from the
source: `bool Ok = Sem.check(*Program); emitAll(); if (!Ok) return 1;`, with
-dump-ast's own branch reachable only past that point) -- so a real probe's
returncode is ground truth for whatever `.Ok` was, and stderr's diagnostic
text is ground truth for hasError/hasWarning substrings.

Design, general enough to cover both the ~150 single-check TEST() bodies and
the ~11 bodies with MULTIPLE independent check() calls (Builtins's
ArityComesFromTheCatalogue and AProgramMayDeclareItsOwnNameOverOneOfAnother
Dialect, SemaEP's EPConstantsNotVisibleIn7185, and all 9 DialectGating cases,
which test the SAME source under two different dialects/outcomes via a
shared `const char* Src = "...";` local):

  1. Find every top-level `check(` call site in the body, in textual order.
  2. Partition the body into windows: call i owns [start_i, start_{i+1}) (or
     end-of-body for the last call) -- this correctly scopes each call's own
     assertions regardless of whether they're written as an inline chain
     (`EXPECT_TRUE(check(...).Ok)`) or via a captured variable
     (`auto R = check(...); EXPECT_TRUE(R.Ok); EXPECT_TRUE(R.hasWarning(...));`),
     since in both shapes everything asserted about one call's result is
     textually written before the NEXT check() call begins.
  3. Within each window, find every `EXPECT_TRUE(...)`/`EXPECT_FALSE(...)`
     call (proper paren-matched, not a naive regex) and classify its content
     as `.Ok`, `.hasError("sub")`, or `.hasWarning("sub")`.
  4. Resolve the call's own source argument: either a direct string literal
     (with C++ adjacent-literal concatenation), or a bare identifier that
     resolves to an earlier `const char* NAME = "...";` / `std::string NAME
     = "...";` local in the same body.
  5. One call site == one .pas file. A body with N independent call sites
     produces N .pas files (suffixed -case1, -case2, ... in call order).

A handful of shapes are NOT handled and are hand-converted/left as a
documented permanent exception: the two Builtins X-macro loop tests, which
drive `check()` from an #include'd .def table rather than a fixed literal
set and have no natural single-.pas-file translation.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_FILE = os.path.join(REPO_ROOT, "test", "Sema", "sema_test.cpp")
OUT_ROOT = os.path.join(REPO_ROOT, "test-lit", "Sema")
PLANG_BIN = os.path.join(REPO_ROOT, "build", "bin", "plang")

# Permanent GoogleTest exceptions -- X-macro loops over Builtins.def, no
# natural Pascal-source trigger (see the trimmed sema_test.cpp's own header).
SKIP = {
    ("Builtins", "ANameOfAnotherDialectIsDeclaredRatherThanUndefined"),
    ("Builtins", "ANameOfThisDialectIsNotRefusedForBeingOne"),
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


def find_matching_paren(text: str, open_pos: int) -> int:
    """open_pos indexes the '(' itself; returns the index of its match."""
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
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    raise ValueError("unbalanced parens")


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


def split_top_level_args(body: str, open_paren: int, close_paren: int) -> list[str]:
    args = []
    i = open_paren + 1
    start = i
    depth = 1
    while i < close_paren:
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
        elif c == "," and depth == 1:
            args.append(body[start:i])
            start = i + 1
        i += 1
    args.append(body[start:close_paren])
    return args


def find_local_src_decl(body: str, varname: str) -> str | None:
    m = re.search(
        rf"(?:const\s+char\s*\*|std::string)\s+{re.escape(varname)}\s*=\s*",
        body)
    if not m:
        return None
    return decode_cpp_string_concat(body[m.end():]) or None


def resolve_check_source(body: str, open_paren: int, close_paren: int):
    """Returns (content, is_ep) or (None, False) if arg1 isn't resolvable."""
    args = split_top_level_args(body, open_paren, close_paren)
    if not args:
        return None, False
    arg1 = args[0].strip()
    if arg1.startswith('"'):
        content = decode_cpp_string_concat(arg1)
    elif re.fullmatch(r"\w+", arg1):
        content = find_local_src_decl(body, arg1)
        if content is None:
            return None, False
    else:
        return None, False
    is_ep = len(args) > 1 and "epOpts()" in args[1]
    return content, is_ep


_ERR_RE = re.compile(r'\.hasError\(\s*"((?:[^"\\]|\\.)*)"\s*\)')
_WARN_RE = re.compile(r'\.hasWarning\(\s*"((?:[^"\\]|\\.)*)"\s*\)')

EXPECT_CALL_RE = re.compile(r"EXPECT_(TRUE|FALSE)\(")
AUTO_CHECK_RE = re.compile(r"\bauto\s+(\w+)\s*=\s*check\(")


def _classify_into(group: dict, span: str, truth: bool) -> None:
    """`span` is one EXPECT_TRUE/FALSE(...) call's argument text. An
    EXPECT_FALSE(...hasError(...)...) is only ever the two Builtins
    X-macro-loop tests (already routed to SKIP), so it's simply ignored here
    rather than modeled -- same for EXPECT_FALSE(...hasWarning...), which
    never occurs at all.
    """
    em = _ERR_RE.search(span)
    wm = _WARN_RE.search(span)
    if em:
        if truth:
            group["err_subs"].append(unescape_cpp(em.group(1)))
    elif wm:
        if truth:
            group["warn_subs"].append(unescape_cpp(wm.group(1)))
    elif re.search(r"\.Ok\b", span):
        if truth:
            group["ok_true"] = True
        else:
            group["ok_false"] = True


def _new_group(check_open: int, check_close: int) -> dict:
    return {"open": check_open, "close": check_close, "ok_true": False,
            "ok_false": False, "err_subs": [], "warn_subs": []}


def probe(content: str, dialect_flag: str) -> tuple[int, str]:
    """Run the real plang -dump-ast against content. Ground truth."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pas", delete=False) as f:
        f.write(content)
        path = f.name
    try:
        args = [PLANG_BIN, "-dump-ast"]
        if dialect_flag:
            args.append(dialect_flag.strip())
        args.append(path)
        r = subprocess.run(args, capture_output=True, text=True, timeout=10)
        return r.returncode, r.stderr
    finally:
        os.remove(path)


def build_case(content: str, is_ep: bool, ok_true: bool, ok_false: bool,
               err_subs: list[str], warn_subs: list[str]) -> str | None:
    if ok_true and ok_false:
        return None
    if not ok_true and not ok_false and not err_subs and not warn_subs:
        return None  # nothing was actually asserted about this call

    dialect_flag = " -std=iso10206" if is_ep else ""
    plang = "%plang_ep" if is_ep else "%plang"
    rc, stderr = probe(content, dialect_flag)

    # Cross-check every claim against the real binary before trusting it.
    if err_subs and rc == 0:
        return None
    if ok_true and rc != 0:
        return None
    if ok_false and rc == 0:
        return None
    for sub in err_subs + warn_subs:
        if sub not in stderr:
            return None

    expect_nonzero = ok_false or bool(err_subs) or rc != 0
    needs_stderr = bool(err_subs) or bool(warn_subs)

    # A module-declaring source makes -dump-ast's own Frontend.cpp write a
    # .pmi file next to whatever path it was given, as a side effect of a
    # successful Sema run -- confirmed the hard way once already (Phase 3's
    # EP/Module migration hit the identical issue: real .pmi files landing in
    # the checked-in test-lit/ tree). split-file relocates the compile into
    # %t.dir so that side effect lands in the build tree, never the source
    # tree, even for a single-chunk "file" -- same idiom
    # test-lit/Module/EP13Modules/*.pas already established.
    declares_module = bool(re.search(r"^\s*module\s", content, re.MULTILINE))
    input_path = "%t.dir/test.pas" if declares_module else "%s"

    # %plang_ep already bakes in -std=iso10206 (test-lit/lit.cfg.py) -- the
    # dialect_flag above is only for the raw-binary probe() call, which
    # doesn't go through that substitution.
    run_lines = []
    if declares_module:
        run_lines.append("RUN: split-file %s %t.dir")
    if needs_stderr:
        cmd = f"{plang} -dump-ast {input_path} 2> %t.err"
        if not (ok_true or ok_false):
            # Original test never asserted on the exit path's shape, only on
            # a stderr fact -- don't invent an exit-code expectation for it.
            cmd += "; true"
        elif expect_nonzero:
            cmd = f"not {cmd}"
        run_lines.append(f"RUN: {cmd}")
        run_lines.append("RUN: FileCheck %s < %t.err")
    else:
        cmd = f"{plang} -dump-ast {input_path}"
        if expect_nonzero:
            cmd = f"not {cmd}"
        run_lines.append(f"RUN: {cmd}")

    out = ["(*"]
    out.extend(run_lines)
    out.append("*)")
    out.append("")
    if declares_module:
        out.append("//--- test.pas")
    out.append(content.rstrip("\n"))
    out.append("")
    if needs_stderr:
        out.append("(*")
        for sub in err_subs + warn_subs:
            out.append(f"CHECK: {sub}")
        out.append("*)")
        out.append("")
    return "\n".join(out)


def build_pas_files(suite: str, name: str, body: str):
    """Returns a list of (suffix, content) pairs, or None on any failure.

    One forward pass over the body, merging two event streams in textual
    order: `auto NAME = check(...)` declarations (open a new group, tracked
    as "the current capture") and `EXPECT_TRUE/FALSE(...)` calls, which
    either directly wrap their own `check(...)` (an inline-chain group,
    self-contained -- e.g. `EXPECT_TRUE(check(SRC).Ok)`) or reference the
    current capture by name (e.g. `EXPECT_TRUE(R.hasError("x"))`, attached to
    whichever `auto R = check(...)` most recently preceded it). This mirrors
    exactly how a reader resolves `R` while reading top-to-bottom -- no
    separate window-boundary computation needed.
    """
    groups: list[dict] = []
    current_capture: tuple[str, int] | None = None

    events = []
    for m in AUTO_CHECK_RE.finditer(body):
        events.append((m.start(), "decl", m.group(1), m.end() - 1))
    for m in EXPECT_CALL_RE.finditer(body):
        events.append((m.start(), "expect", m.group(1) == "TRUE", m.end() - 1))
    events.sort(key=lambda e: e[0])

    for ev in events:
        if ev[1] == "decl":
            _, _, varname, check_open = ev
            try:
                check_close = find_matching_paren(body, check_open)
            except ValueError:
                return None
            groups.append(_new_group(check_open, check_close))
            current_capture = (varname, len(groups) - 1)
            continue

        _, _, truth, open_paren = ev
        try:
            close_paren = find_matching_paren(body, open_paren)
        except ValueError:
            continue
        span = body[open_paren + 1:close_paren]
        check_m = re.search(r"\bcheck\(", span)
        if check_m:
            check_open = open_paren + 1 + (check_m.end() - 1)
            try:
                check_close = find_matching_paren(body, check_open)
            except ValueError:
                return None
            g = _new_group(check_open, check_close)
            _classify_into(g, span, truth)
            groups.append(g)
            current_capture = None
        else:
            if current_capture is None:
                continue
            varname, gi = current_capture
            if not re.search(rf"\b{re.escape(varname)}\.", span):
                continue
            _classify_into(groups[gi], span, truth)

    if not groups:
        return None

    results = []
    for g in groups:
        content, is_ep = resolve_check_source(body, g["open"], g["close"])
        if content is None:
            return None
        pas = build_case(content, is_ep, g["ok_true"], g["ok_false"],
                          g["err_subs"], g["warn_subs"])
        if pas is None:
            return None
        results.append(pas)

    if len(results) == 1:
        return [(None, results[0])]
    return [(f"-case{i+1}", pas) for i, pas in enumerate(results)]


def main():
    text = open(SRC_FILE).read()
    converted = 0
    skipped = 0
    failed = []
    for suite, name, body in split_tests(text):
        if (suite, name) in SKIP:
            skipped += 1
            continue

        files = build_pas_files(suite, name, body)
        if files is None:
            failed.append((suite, name))
            continue

        out_dir = os.path.join(OUT_ROOT, suite)
        os.makedirs(out_dir, exist_ok=True)
        base = kebab(name)
        for suffix, pas in files:
            out_path = os.path.join(out_dir, base + (suffix or "") + ".pas")
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
