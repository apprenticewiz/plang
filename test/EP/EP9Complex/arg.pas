(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1.5708
*)

program p;
var c: complex;
    a: real;
begin
  c := cmplx(0.0, 1.0);
  a := arg(c);
  writeln(a:1:4)
end.
