(*
CRITICAL regression check for TP `Int`/`Frac` (Builtins.def, -std=turbo
only): both take and return a REAL, so -- unlike Trunc/Round, which convert
to the ordinal Integer and therefore range-check their result against
int64_t (runtime/plang_math.cpp's plang_trunc/plang_round,
plang_err_real_to_int_range) -- there is no range to respect.
runtime/plang_math.cpp's plang_tp_int/plang_tp_frac are deliberately NEW
functions built directly on std::trunc, NOT the EXISTING, range-checked
plang_trunc/plang_round reused and cast back to double: doing that would
silently reintroduce Trunc/Round's own int64 range check for a function the
language gives none, ABORTING the program for any |X| >= 2^63 instead of
answering the (perfectly well-defined, already-integral at that magnitude)
Real value -- exactly the class of subtle Turbo-compat regression this
project's own adversarial-review discipline exists to catch, and exactly
what this test is a permanent regression gate against.

A successful (exit 0, complete output) run of this test IS the "does not
crash" proof; the printed comparisons additionally confirm the answers are
actually correct, not merely non-crashing.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:int-1e30-is-unchanged=TRUE
CHECK-NEXT:frac-1e30-is-zero=TRUE
CHECK-NEXT:int-1.5e20-is-unchanged=TRUE
CHECK-NEXT:frac-1.5e20-is-zero=TRUE
*)

program p;
var r: Real;
begin
  r := 1.0e30;
  writeln('int-1e30-is-unchanged=', Int(r) = r);
  writeln('frac-1e30-is-zero=', Frac(r) = 0.0);

  r := 1.5e20;
  writeln('int-1.5e20-is-unchanged=', Int(r) = r);
  writeln('frac-1.5e20-is-zero=', Frac(r) = 0.0);
end.
