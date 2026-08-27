(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:( 1.0000000000000000e-155, 0.0000000000000000e+000)
*)

program p(output);
var c: complex;
begin
  { The naive (ac+bd)/(c^2+d^2) formula squares the divisor's components
    first: 1e155 squared overflows double range to +Inf, so the correct,
    tiny-but-nonzero quotient silently became (0, 0) instead.  A
    numerically stable division (e.g. Smith's algorithm) must not square
    an operand whose magnitude alone is still safely representable. }
  c := cmplx(1.0, 1.0) / cmplx(1.0e155, 1.0e155);
  writeln(c)
end.
