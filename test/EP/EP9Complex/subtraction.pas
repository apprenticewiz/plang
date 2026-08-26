(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.0
CHECK-NEXT:4.0
*)

program p;
var c: complex;
begin
  c := cmplx(5.0, 7.0) - cmplx(2.0, 3.0);
  writeln(re(c):1:1);
  writeln(im(c):1:1)
end.
