(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.14
CHECK-NEXT:0.0
*)

program p;
var c: complex;
begin
  c := 3.14;
  writeln(re(c):1:2);
  writeln(im(c):1:1)
end.
