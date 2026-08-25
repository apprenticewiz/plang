(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
CHECK-NEXT:10 20 30
*)

program p(output);
type pt = record x, y: integer end;
function mk = result: pt;
begin result.x := 1; result.y := 2 end;
function mkArr = result: array[1..3] of integer;
begin result[1] := 10; result[2] := 20; result[3] := 30 end;
var v: pt; a: array[1..3] of integer;
begin
  v := mk; writeln(v.x:1, ' ', v.y:1);
  a := mkArr; writeln(a[1]:1, ' ', a[2]:1, ' ', a[3]:1)
end.
