(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1 2 3
*)

program p(output);
var i, n: integer;
begin
  n := 3;
  for i := 1 to n do begin write(i:2); n := 99 end;
  writeln
end.
