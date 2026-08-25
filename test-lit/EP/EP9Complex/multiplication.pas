(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-5.0
CHECK-NEXT:10.0
*)

program p;
var c: complex;
begin
  c := cmplx(1.0, 2.0) * cmplx(3.0, 4.0);
  writeln(re(c):1:1);
  writeln(im(c):2:1)
end.
