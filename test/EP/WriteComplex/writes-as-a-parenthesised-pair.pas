(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:( 3.0000000000000000e+000, 4.0000000000000000e+000)
*)

program p(output); var a: complex;
begin a := cmplx(3.0, 4.0); writeln(a) end.
