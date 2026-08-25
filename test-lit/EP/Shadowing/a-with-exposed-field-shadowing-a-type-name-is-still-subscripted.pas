(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p(output);
type widget = integer;
     box = record widget: array[1..3] of integer end;
var b: box;
begin
  b.widget[1] := 7;
  with b do writeln(widget[1])
end.
