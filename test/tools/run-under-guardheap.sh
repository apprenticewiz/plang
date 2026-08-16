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
# --self-test: does the gate still do what it claims?
#
# It once did not.  gh_head_of dereferenced the page BEFORE any pointer handed
# to free(), to look for its magic -- and for a pointer this allocator never
# handed out that page need not be mapped, so a plain `malloc(1<<20); free(p)`
# died here.  The gate reported SIGSEGV on correct programs, and it was being
# used to decide whether real defects were real.
#
# So it checks itself now, in both directions: a valid program must survive, and
# a one-byte over-run must still fault.  A gate that only knows how to say
# "fault" is indistinguishable from a broken one.
if [ "${1:-}" = "--self-test" ]; then
    d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
    cat > "$d/ok.c" <<'EOF'
#include <stdlib.h>
int main(void){ for(int i=0;i<4;i++){ void*p=malloc(1<<20); ((char*)p)[0]=1; free(p);} 
                void*c=calloc(1,32); ((char*)c)[31]='x'; free(c); return 0; }
EOF
    cat > "$d/bad.c" <<'EOF'
#include <stdlib.h>
int main(void){ char*p=calloc(1,32); p[31]='x'; p[32]='y'; free(p); return 0; }
EOF
    cc -O0 -o "$d/ok" "$d/ok.c"   || exit 2
    cc -O0 -o "$d/bad" "$d/bad.c" || exit 2
    env LD_PRELOAD="$so" "$d/ok" >/dev/null 2>&1
    ok=$?
    env LD_PRELOAD="$so" "$d/bad" >/dev/null 2>&1
    bad=$?
    fail=0
    if [ "$ok" -ne 0 ]; then
        echo "guardheap FALSE POSITIVE: a valid program exited $ok" >&2; fail=1
    fi
    if [ "$bad" -eq 0 ]; then
        echo "guardheap FALSE NEGATIVE: a one-byte over-run exited 0" >&2; fail=1
    fi
    [ "$fail" -eq 0 ] && echo "guardheap self-test: valid program clean, over-run caught"
    exit "$fail"
fi

exec env LD_PRELOAD="$so" "$@"
