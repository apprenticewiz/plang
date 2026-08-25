(*
R3.  A schema body's bounds are carried to codegen as a closed form over
the discriminants BY INDEX, with every other leaf folded in the scope the
schema was declared in.  The form contains no identifier, so there is
nothing left for a procedure's locals to capture at the allocation site
-- the defect 0.1.6 shipped a scope barrier to guard against.
*)

(* Non-trivial arithmetic on both bounds, over two discriminants and a named
   constant, so that the form is exercised rather than reduced to a literal:
   lo*2-1 = 3 and hi*hi+k = 12 for new(q, 2, 3). *)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 12
*)

program p(output);
const k = 3;
type v(lo, hi: integer) = array[lo*2 - 1 .. hi*hi + k] of integer;
var q: ^v; i: integer;
begin new(q, 2, 3);
  for i := 3 to 12 do q^[i] := i;
  writeln(q^[3]:1, ' ', q^[12]:1) end.
