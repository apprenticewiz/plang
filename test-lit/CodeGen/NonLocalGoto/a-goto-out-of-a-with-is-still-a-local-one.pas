(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:yes
*)

program p(output);
label 3;
type r = record a: integer end;
var v: r;
begin
  v.a := 1;
  with v do begin if a = 1 then goto 3; writeln('no') end;
3:
  writeln('yes')
end.
