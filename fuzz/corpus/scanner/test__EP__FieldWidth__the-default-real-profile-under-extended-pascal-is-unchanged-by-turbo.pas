(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Non-regression baseline for the Turbo dialect's own default real-write
   profile (a different ExpChar -- see test/Driver/Turbo): pins Extended
   Pascal's default (width 24, 17 significant digits, 3-digit exponent,
   lowercase 'e' -- the same as ISO 7185's own, see the sibling baseline in
   test/CodeGen/WriteReal) byte-for-byte across zero, a small
   integer-valued real, a very small magnitude, a very large magnitude, and
   a negative real. *)

(*
CHECK: 0.0000000000000000e+000
CHECK-NEXT: 2.0000000000000000e+000
CHECK-NEXT: 1.0000000000000000e-010
CHECK-NEXT: 1.0000000000000000e+308
CHECK-NEXT:-3.5000000000000000e+000
*)

program p(output);
begin
  writeln(0.0);
  writeln(2.0);
  writeln(1.0e-10);
  writeln(1.0e308);
  writeln(-3.5)
end.
