(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2.0
CHECK-NEXT:0.0
*)

program p;
var c: complex;
begin
  c := polar(2.0, 0.0);
  writeln(re(c):1:1);
  writeln(im(c):1:1)
end.
