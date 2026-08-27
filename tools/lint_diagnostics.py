#!/usr/bin/env python3
"""lint_diagnostics.py -- find diagnostic IDs that are declared but never emitted.

Every diagnostic plang can report is declared once, as a `DIAG(id, Level, "msg")`
line in one of the *Kinds.def files under include/plang/Basic/ (Driver, Lex, Parse,
Sema), and is meant to be raised from compiler source via `diag::<id>` wherever the
condition it names is detected. A declaration with no such call site anywhere is at
best dead cruft left behind by a removed or refactored check; at worst it is a "lost
check" -- the id exists as if reserving the behavior, but nothing actually enforces
it, so a Pascal program that should be rejected compiles clean instead.

Issue #295 found this mechanically reproducible pattern in three declared ids, none
of which had a single emitter anywhere in lib/:

  - err_readln_not_text: superseded in place by the more general
    err_line_proc_not_text (readln/writeln/page/eoln all need a text file, not just
    readln) but never deleted once the general check subsumed it.
  - err_schema_disc_type_mismatch: named for exactly the schema-discriminant type
    check issue #149 added to SemaType.cpp's schema instantiation loop, but that fix
    landed using two other, already-existing ids (err_schema_new_disc_type for the
    ordinal check, err_assign_mismatch for the assign-compatibility check) instead of
    this one -- the check is real and wired up, just never under this id.
  - err_comment_delim_mismatch: ISO 7185 6.1.8 note 1 makes '{' and '(*' each
    closeable by either '}' or '*)' ("either terminator ends either"), which
    Scanner::skipComment implements deliberately -- a delimiter mismatch is valid
    Pascal, not an error, so this id names a check that would have been wrong to add.

All three turned out to be dead cruft (safe to delete) rather than a check actually
missing -- but that will not always be true of whatever this lint finds next, which
is why this script only reports; it does not delete anything itself.

Declared-vs-emitted, not declared-vs-mentioned: only a `diag::<id>` token under lib/
(the compiler's real implementation) counts as an emission site. This deliberately
excludes test/unittests/Basic/catalog_test.cpp, which drives the catalog/localization
machinery through a couple of arbitrarily-chosen diagnostic ids as fixtures -- that
exercises the lookup, not the check the id is supposed to name -- and both of the ids
it uses already have real lib/ emitters regardless.

Exit code is nonzero iff --strict is passed and any finding exists; otherwise this
always exits 0 and just reports (matches tools/lint_test.py's convention).
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASIC_DIR = os.path.join(REPO_ROOT, "include", "plang", "Basic")
DISPATCHER = os.path.join(BASIC_DIR, "DiagnosticMessages.def")
DEFAULT_SEARCH_ROOTS = [os.path.join(REPO_ROOT, "lib")]

INCLUDE_RE = re.compile(r'#include\s+"plang/Basic/(\w+\.def)"')
DIAG_DECL_RE = re.compile(r'^DIAG\(\s*([A-Za-z_]\w*)\s*,')
DIAG_REF_RE = re.compile(r'diag::([A-Za-z_]\w*)')


def declared_ids() -> dict[str, tuple[str, int]]:
    """Map diagnostic id -> (relpath, lineno) of its DIAG(...) declaration.

    The set of *Kinds.def files to scan is discovered from DiagnosticMessages.def's
    own #include list instead of being hardcoded here a second time, so a renamed or
    newly added Kinds.def file is picked up automatically. DiagnosticMessages.def's
    own header comment calls itself the "master dispatcher for all plang
    diagnostics" for exactly this reason.
    """
    dispatcher_text = open(DISPATCHER, encoding="utf-8").read()
    kinds_files = INCLUDE_RE.findall(dispatcher_text)
    if not kinds_files:
        raise RuntimeError(f"no *Kinds.def #includes found in {DISPATCHER}")

    ids: dict[str, tuple[str, int]] = {}
    for name in kinds_files:
        path = os.path.join(BASIC_DIR, name)
        rel = os.path.relpath(path, REPO_ROOT)
        with open(path, encoding="utf-8") as f:
            for lineno, line in enumerate(f, start=1):
                m = DIAG_DECL_RE.match(line)
                if m:
                    ids[m.group(1)] = (rel, lineno)
    return ids


def referenced_ids(paths: list[str]) -> set[str]:
    """Every id named by a `diag::<id>` token found under `paths` (default: lib/)."""
    refs: set[str] = set()
    for root in paths:
        if os.path.isfile(root):
            files = [root]
        else:
            files = []
            for ext in ("*.cpp", "*.h", "*.hpp"):
                files.extend(glob.glob(os.path.join(root, "**", ext), recursive=True))
        for path in files:
            text = open(path, encoding="utf-8").read()
            refs.update(DIAG_REF_RE.findall(text))
    return refs


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true",
                     help="exit nonzero if any finding exists")
    ap.add_argument("paths", nargs="*", default=DEFAULT_SEARCH_ROOTS,
                     help="files/directories to search for diag:: emission sites "
                          "(default: lib/)")
    args = ap.parse_args()

    declared = declared_ids()
    referenced = referenced_ids(args.paths)
    searched = ", ".join(os.path.relpath(p, REPO_ROOT) for p in args.paths)

    orphans = sorted(set(declared) - referenced)
    for id_ in orphans:
        rel, lineno = declared[id_]
        print(f"{rel}:{lineno}: '{id_}' is declared but never emitted "
              f"(no diag::{id_} found under {searched})")
    print(f"\n{len(orphans)} orphaned diagnostic(s) of {len(declared)} declared, "
          f"searched {searched}")

    if args.strict and orphans:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
