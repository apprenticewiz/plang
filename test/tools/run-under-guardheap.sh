#!/usr/bin/env bash
# Run a plang-compiled program with every heap object butted against a guard
# page, so a write one byte past the end of a schema body faults at the
# instruction that makes it.
#
# Why this exists: no other gate in this tree can see a heap over-run in a
# GENERATED program.  The AddressSanitizer build instruments the compiler, not
# its output; the IR-identity gate is ISO 7185 only; and every schema test's
# oracle is a printed value, which a corrupted NEIGHBOURING field does not
# change.  The fourth review found over-runs that printed correct output and
# exited 0.
#
#   test/tools/run-under-guardheap.sh ./myprog [args...]
#
# Exit 139 (SIGSEGV) means the program wrote outside an allocation.  Compare
# against a plain run: if that one exits 0, the corruption was silent.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
so=${GUARDHEAP_SO:-${TMPDIR:-/tmp}/plang-guardheap-$(id -u).so}
if [ ! -f "$so" ] || [ "$here/guardheap.c" -nt "$so" ]; then
    cc -shared -fPIC -O1 -o "$so" "$here/guardheap.c" -ldl || exit 2
fi
exec env LD_PRELOAD="$so" "$@"
