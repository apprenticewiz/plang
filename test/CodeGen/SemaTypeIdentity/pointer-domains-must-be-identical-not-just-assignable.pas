(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* Regression test for issue #172: isAssignCompatible's pointer case recursed
   into ordinary assignment compatibility on the two pointees instead of
   requiring their domain types to be identical.  A subrange is assignment-
   compatible with its own base type in both directions (that is what lets a
   literal or a wider value be assigned into a subrange variable elsewhere),
   so that recursion let `^integer` and `^(1..10)` be assigned to one another
   even though ISO 7185 §6.4.4 requires a pointer assignment's two types to
   have the SAME domain type -- no covariance through a subrange's base type,
   in either direction. *)

program p;
type
  sub = 1..10;
var
  a: ^integer;
  b: ^sub;
begin
  a := b;
  b := a
end.

(*
CHECK: cannot assign
CHECK: cannot assign
*)
