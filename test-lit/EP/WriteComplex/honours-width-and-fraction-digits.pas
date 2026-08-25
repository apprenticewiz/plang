(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:(    3.00,   -4.50)
*)

program p(output); var a: complex;
begin a := cmplx(3.0, -4.5); writeln(a:8:2) end.
