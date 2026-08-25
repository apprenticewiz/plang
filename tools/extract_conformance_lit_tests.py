#!/usr/bin/env python3
"""Extract test/Conformance/cases/*.cpp (377 GoogleTest cases, one per file,
issue #34 Phase 1) into standalone .pas files under test-lit/Conformance/.

Every input file has one fixed, machine-generated shape (see
test/Conformance/CMakeLists.txt's own comment: produced by the now-lost
tools/gen_conformance_tests.py):

    TEST(ConformanceError, prt0001) {
        EXPECT_FALSE(check(R"plang(
    ... Pascal source, byte-for-byte ...
    )plang").Ok);
    }

check() (test/Support/TestHelper.h) runs Scanner->Parser->Sema only, with
default LangOptions -- Standard::ISO7185, the same as plang's own bare
no-flag default -- and answers a single Ok bool. `plang -dump-ast` was
confirmed (Phase 0 design work) to run that identical pipeline and exit
nonzero with a real diagnostic on any failure, exit 0 with the AST printed
otherwise -- so the whole assertion becomes a bare RUN line, no FileCheck
needed:

    ConformanceClean (EXPECT_TRUE)  -> RUN: %plang -dump-ast %s
    ConformanceError (EXPECT_FALSE) -> RUN: not %plang -dump-ast %s

This script is intentionally scoped to this one uniform shape (regex, not a
general brace-depth scanner) -- Conformance's own generator already produced
maximally regular output, one case per file. A harder migration phase with
more varied hand-written test bodies will need something more general; don't
build that generality here before it's needed.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CASES_DIR = REPO_ROOT / "test" / "Conformance" / "cases"
OUT_ROOT = REPO_ROOT / "test-lit" / "Conformance"

CASE_RE = re.compile(
    r'TEST\((?P<suite>ConformanceClean|ConformanceError),\s*(?P<name>\w+)\)\s*\{\s*'
    r'EXPECT_(?P<verdict>TRUE|FALSE)\(check\(R"plang\((?P<src>.*?)\)plang"\)\.Ok\);\s*'
    r'\}\s*$',
    re.DOTALL,
)


def extract_one(path: Path) -> tuple[str, str]:
    text = path.read_text()
    m = CASE_RE.search(text)
    if not m:
        raise ValueError(f"{path}: did not match the expected single-TEST() shape")
    suite = m.group("suite")
    name = m.group("name")
    verdict = m.group("verdict")
    src = m.group("src")

    stem = path.stem
    if name != stem:
        raise ValueError(f"{path}: TEST name {name!r} != filename stem {stem!r}")

    expect_clean = suite == "ConformanceClean"
    if expect_clean != (verdict == "TRUE"):
        raise ValueError(f"{path}: suite {suite} and verdict EXPECT_{verdict} disagree")

    subdir = "Clean" if expect_clean else "Error"
    run_line = "RUN: %plang -dump-ast %s" if expect_clean else "RUN: not %plang -dump-ast %s"

    # src starts and ends with the newlines that sat right after '(' and
    # right before ')' in the raw string -- strip exactly one leading and
    # one trailing newline (matching R"plang(\n...\n)plang"'s own shape)
    # rather than a blanket .strip(), which would also eat meaningful
    # leading/trailing blank lines inside the original PRT source.
    if src.startswith("\n"):
        src = src[1:]
    if src.endswith("\n"):
        src = src[:-1]

    pas = f"(*\n{run_line}\n*)\n\n{src}\n"
    return subdir, pas


def main() -> int:
    check_only = "--check" in sys.argv
    cpp_files = sorted(CASES_DIR.glob("prt*.cpp"))
    if not cpp_files:
        print(f"no prt*.cpp files found under {CASES_DIR}", file=sys.stderr)
        return 1

    errors = []
    written = 0
    for cpp in cpp_files:
        try:
            subdir, pas = extract_one(cpp)
        except ValueError as e:
            errors.append(str(e))
            continue
        out_dir = OUT_ROOT / subdir
        out_path = out_dir / (cpp.stem + ".pas")
        if check_only:
            existing = out_path.read_text() if out_path.exists() else None
            if existing != pas:
                errors.append(f"{out_path}: would change (run without --check to write)")
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path.write_text(pas)
        written += 1

    if errors:
        print(f"{len(errors)} problem(s):", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1

    if check_only:
        print(f"OK: all {len(cpp_files)} cases already match their generated .pas file")
    else:
        print(f"Wrote {written} .pas files from {len(cpp_files)} source cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
