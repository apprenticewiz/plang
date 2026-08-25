(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: has no result type
ERR-ABSENT-NOT: internal error
*)

program p;
function f;
begin f := 1 end;
begin writeln(f) end.
