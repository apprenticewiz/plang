(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-8 -1 2 8 
CHECK-NEXT:-1 2 
CHECK-NEXT:-8 
CHECK-NEXT:-8 8 
CHECK-NEXT:true false
*)

program p;
type r = -8..8; s = set of r;
var a, b: s; i: r;
begin
  a := [-8, -1, 2]; b := [-1, 2, 8];
  for i in a + b do write(i, ' '); writeln;
  for i in a * b do write(i, ' '); writeln;
  for i in a - b do write(i, ' '); writeln;
  for i in a >< b do write(i, ' '); writeln;
  writeln(a <= a + b, ' ', a = b)
end.
