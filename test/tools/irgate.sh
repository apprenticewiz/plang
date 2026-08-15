#!/usr/bin/env bash
#
# IR byte-identity gate.
#
# Emits LLVM IR for every conformance case and for iso7185pat.pas, at a known-
# good commit and at the working tree, and diffs the two.  A change that is
# meant to be semantics-preserving should move NO bytes; one that is not should
# move exactly the bytes it claims to.
#
# This exists because the test suite's oracle is a printed value, and a layout
# that is wrong but self-consistent prints the right value.  Comparing the IR
# compares what the compiler DID, not what one program happened to observe.
#
# Usage:  test/tools/irgate.sh [baseline-commit]
#
# It checks out the baseline, so it MUST put the tree back.  It once did that
# with a plain checkout at the end; a machine crash mid-run left the repository
# detached at the baseline with the day's work apparently gone (it was not --
# it was committed on the branch), which cost more time than the gate saves.
# Hence the trap: any exit, interrupt or failure returns to where you were.
set -euo pipefail

R=$(git rev-parse --show-toplevel)
cd "$R"

BASE=${1:-cfb1508}          # the 0.1.5 release point
W=${TMPDIR:-/tmp}/plang-irgate-$$
BUILD=build

# Where to come back to, by name when there is one so the tree is not left
# detached, and by hash when HEAD is already detached.
WAS=$(git symbolic-ref --quiet --short HEAD || git rev-parse HEAD)
restore() { git checkout -q "$WAS" 2>/dev/null || true; }
trap restore EXIT INT TERM

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "working tree is dirty -- commit or stash first, or the checkout will refuse" >&2
    exit 2
fi

rm -rf "$W"; mkdir -p "$W/src" "$W/before" "$W/after"

# Extract every program from the conformance cases, plus the acceptance test.
python3 - "$W/src" <<'PY'
import re, os, glob, sys
W = sys.argv[1]; n = 0
for f in sorted(glob.glob("test/Conformance/cases/*.cpp")):
    t = open(f, errors="ignore").read()
    for m in re.finditer(r'R"plang\((.*?)\)plang"', t, re.S):
        p = m.group(1)
        if 'program' not in p.lower(): continue
        open(os.path.join(W, f"c{n:04d}.pas"), "w").write(p); n += 1
acc = "test/Acceptance/cases/iso7185pat.pas"
if os.path.exists(acc):
    open(os.path.join(W, f"c{n:04d}.pas"), "w").write(open(acc, errors="ignore").read()); n += 1
print("extracted", n, "programs")
PY

emit() {
    for f in "$W"/src/*.pas; do
        b=$(basename "$f" .pas)
        for std in iso7185 iso10206; do
            ./$BUILD/tools/driver/plang -std=$std -emit-llvm "$f" -o "$1/$b.$std.ll" \
                >/dev/null 2>&1 || true
        done
    done
    echo "  $(ls "$1" | wc -l) modules"
}

echo "=== BEFORE ($BASE) ==="
git checkout -q "$BASE"
cmake --build $BUILD -j"$(nproc)" >/dev/null 2>&1
emit "$W/before"

echo "=== AFTER ($WAS) ==="
git checkout -q "$WAS"
cmake --build $BUILD -j"$(nproc)" >/dev/null 2>&1
emit "$W/after"

echo "=== DIFF ==="
diff -rq "$W/before" "$W/after" > "$W/diff.txt" 2>&1 || true

# Accepted differences.  Each needs a commit and a reason, and each was checked
# mechanically -- substituting the one shape for the other must leave ZERO
# residual diff, or it is not the change it claims to be.
#
#   c0377 (c09a4de): iso7185pat.pas's `vra` record has a variant alternative
#   holding a `cset`.  A set is i256 and wants 16-byte alignment; the variant
#   blob capped its cell at i64, so the run was 8-aligned while codegen emitted
#   `align 16` accesses into it.  The blob is [6 x i128] where it was [11 x i64].
grep -v "c0377\." "$W/diff.txt" > "$W/unexpected.txt" || true

UNEXPECTED=$(grep -c differ "$W/unexpected.txt" || true)
TOTAL=$(grep -c differ "$W/diff.txt" || true)
echo "unexpected: $UNEXPECTED"
echo "accepted:   $((TOTAL - UNEXPECTED)) (c0377 blob retype)"
head -20 "$W/unexpected.txt"
echo "artifacts:  $W"
[ "$UNEXPECTED" -eq 0 ]
