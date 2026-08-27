#!/usr/bin/env python3
"""check_release_version.py -- verify a release tag, VERSION, and the
changelog agree before a release ships (issue #185).

The top-level CMakeLists.txt's "---- Version ----" block documents a manual,
five-step release process: cut/reuse a release branch, set VERSION to the
target number, set PLANG_MANPAGE_DATE, move the changelog's [Unreleased]
section under the new heading, and only then tag. Nothing enforced that a
human actually did steps 2 and 4 for the same number used in step 5 -- a
tag of v0.3.4 cut against a VERSION file still reading 0.3.3 (a forgotten
step 2) built and shipped exactly as "successfully" as a correct release
would have, a compiler reporting one version with a shared library carrying
another and no CI signal either way.

This script is that signal. It performs the following checks; every one
that runs must pass, and a check that could not run (its inputs were not
available) is reported as SKIP, never silently counted as a pass:

  1. TAG FORMAT    -- the tag matches v<MAJOR>.<MINOR>.<PATCH> exactly.

  2. VERSION FILE  -- VERSION's stripped content equals the tag's bare
     number. This stands in for PROJECT_VERSION too, deliberately not
     re-derived via a real (LLVM-dependent) CMake configure just to read it
     back: CMakeLists.txt's `project(plang VERSION ${PLANG_BASE_VERSION}
     ...)` sets PROJECT_VERSION to this exact string with no independent
     source, and lib/CMakeLists.txt's `plang_frontend` target sets its own
     SOVERSION/VERSION properties from PROJECT_VERSION the same way -- so a
     passing check here already is a passing check for both, by
     construction, not by re-measurement.

  3. CHANGELOG     -- CHANGELOG.md has a "## [MAJOR.MINOR.PATCH] - DATE"
     release heading, i.e. the [Unreleased] section was actually moved
     (step 4), not left sitting there while a tag went out under its name.

  4. GIT DESCRIBE  -- only if the named tag actually exists and points at
     HEAD: reproduces the exact regex and logic CMakeLists.txt uses to
     compute PLANG_VERSION_STRING from `git describe`, and confirms it
     comes out to the bare number with no "-N-gHASH" distance suffix and no
     "-dirty" suffix -- i.e. the "displayed version" `plang --version`
     would show for a build done at this exact commit.

  5. BUILT BINARY  -- only if --plang-binary is given: runs
     `<path> -dumpversion` (gcc/clang convention: exactly the version and
     nothing else) and compares it literally. The most direct possible
     check against a real build that "the compiler reports one version".

  6. SHARED LIBRARY -- only if --lib-dir is given: confirms
     libplang-frontend.so.<bare number> exists there. The most direct
     possible check against a real build that "the library carries
     another [version]".

Usage:
  Pre-publish, by hand, right after step 4 (moving the changelog heading)
  and before step 5 (tagging) -- checks the number about to be used:
      python3 tools/check_release_version.py --tag v0.3.5

  In CI, triggered by the tag push itself (step 5 already done, HEAD *is*
  the tag):
      python3 tools/check_release_version.py

  With a real build's artifacts on hand, for the strongest possible check:
      python3 tools/check_release_version.py \\
          --plang-binary build/bin/plang --lib-dir build/lib

Exit status is nonzero iff any check that ran failed.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")

# Mirrors CMakeLists.txt's own PLANG_GIT_DESCRIBE match exactly (see its
# "---- Version ----" block) so this reproduces PLANG_VERSION_STRING's
# computation instead of merely approximating it.
DESCRIBE_RE = re.compile(r"^v?(\d+\.\d+\.\d+)(-(\d+)-g([0-9a-f]+))?(-dirty)?$")

# CHANGELOG headings use "-" almost everywhere and "--" exactly once
# (0.1.2, historical); accept both rather than flag a years-old typo this
# check was never meant to relitigate.
CHANGELOG_HEADING_RE_TMPL = r"^## \[{ver}\] -{{1,2}} \d{{4}}-\d{{2}}-\d{{2}}\s*$"


class Check:
    """One pass/fail/skip result, printed as a single line."""

    def __init__(self, name: str):
        self.name = name
        self.status = "SKIP"
        self.detail = ""

    def passed(self, detail: str = "") -> None:
        self.status, self.detail = "PASS", detail

    def failed(self, detail: str) -> None:
        self.status, self.detail = "FAIL", detail

    def skipped(self, detail: str) -> None:
        self.status, self.detail = "SKIP", detail

    def __str__(self) -> str:
        tag = f"[{self.status:4}]"
        return f"{tag} {self.name}: {self.detail}" if self.detail else f"{tag} {self.name}"


def run_git(repo_root: str, *args: str) -> tuple[int, str]:
    proc = subprocess.run(["git", "-C", repo_root, *args],
                           capture_output=True, text=True)
    return proc.returncode, proc.stdout.strip()


def detect_tag(repo_root: str) -> str | None:
    """The release tag HEAD sits exactly on, or None if it is not exactly
    on one (matches CMakeLists.txt's own --match "v[0-9]*" restriction)."""
    rc, out = run_git(repo_root, "describe", "--tags", "--exact-match",
                       "--match", "v[0-9]*", "HEAD")
    return out if rc == 0 else None


def check_tag_format(tag: str) -> tuple[Check, str | None]:
    c = Check("tag format")
    m = TAG_RE.match(tag)
    if not m:
        c.failed(f"{tag!r} does not match v<MAJOR>.<MINOR>.<PATCH>")
        return c, None
    c.passed(f"{tag} -> bare version {m.group(1)}")
    return c, m.group(1)


def check_version_file(repo_root: str, bare_version: str) -> Check:
    c = Check("VERSION file (PROJECT_VERSION and the library's SOVERSION/VERSION "
              "properties are derived from it, not independent)")
    path = os.path.join(repo_root, "VERSION")
    try:
        with open(path, encoding="utf-8") as f:
            content = f.readline().strip()
    except OSError as e:
        c.failed(f"could not read {path}: {e}")
        return c
    if content != bare_version:
        c.failed(f"{path} holds {content!r}, tag {bare_version!r} wants that changed -- "
                 f"was the release-branch VERSION bump (5-step process, step 2, "
                 f"see CMakeLists.txt) forgotten?")
        return c
    c.passed(f"{path} holds {content!r}")
    return c


def check_changelog(repo_root: str, bare_version: str) -> Check:
    c = Check("CHANGELOG.md release heading")
    path = os.path.join(repo_root, "CHANGELOG.md")
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        c.failed(f"could not read {path}: {e}")
        return c
    heading_re = re.compile(
        CHANGELOG_HEADING_RE_TMPL.format(ver=re.escape(bare_version)), re.M)
    if not heading_re.search(text):
        c.failed(f"no '## [{bare_version}] - YYYY-MM-DD' heading in {path} -- was "
                 f"[Unreleased] moved under it (5-step process, step 4)?")
        return c
    c.passed(f"found '## [{bare_version}] - ...' heading")
    return c


def check_git_describe(repo_root: str, tag: str, bare_version: str) -> Check:
    c = Check("git describe (reproduces PLANG_VERSION_STRING's own computation)")
    rc, head = run_git(repo_root, "rev-parse", "HEAD")
    if rc != 0:
        c.skipped("HEAD could not be resolved (not a git checkout?)")
        return c
    rc, tag_commit = run_git(repo_root, "rev-parse", f"refs/tags/{tag}^{{commit}}")
    if rc != 0:
        c.skipped(f"{tag} does not exist as a real ref yet (pre-publish dry run) "
                  f"-- re-run after `git tag {tag}` for this check")
        return c
    if tag_commit != head:
        c.skipped(f"{tag} does not point at HEAD ({tag_commit[:8]} vs {head[:8]})")
        return c
    rc, describe = run_git(repo_root, "describe", "--tags", "--dirty",
                            "--match", "v[0-9]*")
    if rc != 0:
        c.failed('`git describe --tags --dirty --match "v[0-9]*"` failed')
        return c
    m = DESCRIBE_RE.match(describe)
    if not m:
        c.failed(f"{describe!r} does not match CMakeLists.txt's own describe regex")
        return c
    if m.group(3):
        c.failed(f"HEAD is {m.group(3)} commit(s) past the nearest tag ({describe!r}) "
                 f"-- not exactly on a release tag")
        return c
    if m.group(5):
        c.failed(f"working tree is dirty at release time ({describe!r})")
        return c
    if m.group(1) != bare_version:
        c.failed(f"nearest tag {m.group(1)!r} != {bare_version!r}")
        return c
    c.passed(f"`git describe` == {describe!r}; PLANG_VERSION_STRING will be "
             f"{bare_version!r} exactly")
    return c


def check_plang_binary(binary: str, bare_version: str) -> Check:
    c = Check("built plang binary (-dumpversion)")
    try:
        proc = subprocess.run([binary, "-dumpversion"], capture_output=True,
                               text=True, timeout=30)
    except OSError as e:
        c.failed(f"could not run {binary}: {e}")
        return c
    reported = proc.stdout.strip()
    if proc.returncode != 0 or reported != bare_version:
        c.failed(f"{binary} -dumpversion printed {reported!r}, wanted {bare_version!r}")
        return c
    c.passed(f"{binary} -dumpversion == {reported!r}")
    return c


def check_lib_dir(lib_dir: str, bare_version: str) -> Check:
    c = Check("built shared library (SOVERSION/VERSION target properties)")
    wanted = os.path.join(lib_dir, f"libplang-frontend.so.{bare_version}")
    if not os.path.exists(wanted):
        found = sorted(glob.glob(os.path.join(lib_dir, "libplang-frontend.so*")))
        c.failed(f"{wanted} does not exist (found: {found or 'nothing'})")
        return c
    c.passed(f"{wanted} exists")
    return c


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", default=REPO_ROOT,
                     help="repository root to check (default: this script's own repo)")
    ap.add_argument("--tag",
                     help="the release tag to check, e.g. v0.3.5 (default: the tag "
                          "HEAD is exactly on)")
    ap.add_argument("--plang-binary",
                     help="path to a built plang binary; adds the built-binary check")
    ap.add_argument("--lib-dir",
                     help="directory holding the built libplang-frontend.so.*; adds "
                          "the built-library check")
    args = ap.parse_args()

    tag = args.tag or detect_tag(args.repo_root)
    if not tag:
        print("error: HEAD is not exactly on a release tag (v[0-9]*), and --tag "
              "was not given.")
        print("Pass --tag explicitly to check a release before tagging, e.g.:")
        print("    python3 tools/check_release_version.py --tag v0.3.5")
        return 2

    checks: list[Check] = []
    fmt_check, bare_version = check_tag_format(tag)
    checks.append(fmt_check)
    if bare_version is None:
        for c in checks:
            print(c)
        return 1

    checks.append(check_version_file(args.repo_root, bare_version))
    checks.append(check_changelog(args.repo_root, bare_version))
    checks.append(check_git_describe(args.repo_root, tag, bare_version))
    if args.plang_binary:
        checks.append(check_plang_binary(args.plang_binary, bare_version))
    if args.lib_dir:
        checks.append(check_lib_dir(args.lib_dir, bare_version))

    for c in checks:
        print(c)

    failed = [c for c in checks if c.status == "FAIL"]
    skipped = [c for c in checks if c.status == "SKIP"]
    passed = len(checks) - len(failed) - len(skipped)
    print(f"\n{tag}: {passed} passed, {len(failed)} failed, {len(skipped)} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
