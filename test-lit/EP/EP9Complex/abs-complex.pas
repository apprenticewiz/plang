(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5.0
*)

program p;
var c: complex;
begin
  c := cmplx(3.0, 4.0);
  writeln(abs(c):1:1)
end.
