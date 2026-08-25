(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1000.00
CHECK-NEXT:0.0150
CHECK-NEXT:25000000000.0
CHECK-NEXT:1000.00
*)

program p(output);
const k = 1e3;
var r: real;
begin
  r := 1e3;     writeln(r:0:2);
  r := 1.5E-2;  writeln(r:0:4);
  r := 2.5e+10; writeln(r:0:1);
  r := k;       writeln(r:0:2)
end.
