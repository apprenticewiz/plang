(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2.5
CHECK-NEXT:3.5
*)

program p;
var c: complex;
begin
  c := cmplx(2.5, 3.5);
  writeln(re(c):1:1);
  writeln(im(c):1:1)
end.
