(*
RUN: %plang -fno-nil-checks %s -o %t
RUN: %run %t 2> %t.err; true
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: dereference of nil
*)

(*
Without the check the dereference is not guarded at all, so what happens
next is unspecified -- a straight-line hardware fault at -O0, or folded
away entirely once the optimizer can prove the pointer is null (confirmed
empirically: -O0 SIGSEGVs, -O1 and up exit 0). Either way, the one thing
that must never happen is the runtime's own nil-check trap firing, which
-fno-nil-checks specifically asks to skip -- "; true" tolerates whichever
outcome the compiler picked so the exit code itself is never asserted on,
leaving the ERR-ABSENT-NOT check as the sole, real assertion.
*)

program p;
type pi = ^integer;
var q: pi;
begin q := nil; writeln(q^) end.
