(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:( 4.00000000000000e+000, 6.00000000000000e+000)
*)

program p(output); var a, b: complex;
begin a := cmplx(1.0, 2.0); b := cmplx(3.0, 4.0);
 writeln(a + b) end.
