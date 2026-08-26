(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42.0
CHECK-NEXT:0.0
*)

program p;
var c: complex;
begin
  c := 42;
  writeln(re(c):2:1);
  writeln(im(c):1:1)
end.
