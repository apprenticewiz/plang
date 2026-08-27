(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-0.0000000000000000e+000
CHECK-NEXT:    -0.000
CHECK-NEXT:             -0
*)

(* A negative zero keeps its sign in every real format FPC does, not just the
   fixed-point one: the exponential/default format used to special-case an
   all-zero mantissa back to a positive zero, which was inconsistent with the
   fixed-point format right below it and with what FPC itself writes. *)
program p(output); var x: real;
begin
  x := -0.0;
  writeln(x);
  writeln(x:10:3);
  writeln(x:15:0)
end.
