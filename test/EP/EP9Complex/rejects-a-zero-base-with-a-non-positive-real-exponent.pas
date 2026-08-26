(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: is undefined
*)

{ EP §6.8.3.2's "an error if x is zero and y is less than or equal to zero"
  applies to a complex base the same way it does a real one -- cmplx(0,0)
  raised to a non-positive-real-part power must trap like 0.0 ** -1.0
  already does, not silently answer Inf/NaN. }
program p(output); var c: complex;
begin c := cmplx(0.0, 0.0) ** cmplx(-1.0, 0.0); writeln(c) end.
