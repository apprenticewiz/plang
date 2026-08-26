(*
EP section 6.4.8.  A schema instantiated inside another schema's BODY has
discriminants that are arithmetic over the enclosing ones.  Sema folds
the body against a probe binding of 1, so `vector(n)` inside
`matrix(m,n)` came out `vector(1)` -- and that probe answer was taken for
the layout, so the allocation was one element wide in every instance and
the writes ran off the end of it.

This is the canonical example from the standard itself, which is the
strongest argument for it not being an edge case.
*)

(* The record shape, where the instantiation is a FIELD: k sits behind it,
   so an instantiation sized from the probe shows up as k being overwritten
   rather than as a wrong element. *)

(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:400 99
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     outer(n: integer) = record a: array[1..n] of integer;
                                 x: inner(n); k: integer end;
var q: ^outer; i: integer;
begin new(q, 4);
  for i := 1 to 4 do begin q^.a[i] := i; q^.x[i] := i * 100 end;
  q^.k := 99; writeln(q^.x[4]:1, ' ', q^.k:1) end.
