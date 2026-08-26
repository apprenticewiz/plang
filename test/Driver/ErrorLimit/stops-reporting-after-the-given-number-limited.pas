(*
Paired with ...-unlimited.pas: -ferror-limit=2 stops reporting after
exactly the given number, on the same program that unlimited reports
more than 2 errors for.

RUN: not %plang -ferror-limit=2 %s -o %t 2> %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:2
*)

program p;
begin a:=1; b:=2; c:=3; d:=4 end.
