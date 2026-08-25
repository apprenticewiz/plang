(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:123456
CHECK-NEXT:34
CHECK-NEXT:12
*)

program p;
var a, b, c: set of 1..10; i: integer;
procedure show(s: set of 1..10);
  var j: integer;
begin
  for j := 1 to 10 do if j in s then write(j);
  writeln
end;
begin
  a := [1,2,3,4]; b := [3,4,5,6];
  c := a + b; show(c);
  c := a * b; show(c);
  c := a - b; show(c)
end.
