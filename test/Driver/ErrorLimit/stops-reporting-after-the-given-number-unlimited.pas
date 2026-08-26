(*
Paired with ...-limited.pas: without -ferror-limit, every one of this
program's errors is reported (more than the limit the paired file pins).
Excluding exactly 0/1/2 (rather than pinning the real count, empirically
8) keeps this robust to the diagnostic count itself changing later --
lit's internal shell has no command substitution to compute a real ">2"
comparison directly, so the excluded-values form is the faithful
translation of the original inequality, not an exact-count stand-in.

RUN: not %plang %s -o %t 2> %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT-NOT:0
COUNT-NOT:1
COUNT-NOT:2
*)

program p;
begin a:=1; b:=2; c:=3; d:=4 end.
