(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* A negative element does make checkSetLit derive an explicit subrange
   window from the literal's elements (see the sibling
   literal-set-too-wide_positive-only.pas for the all-non-negative case that
   didn't), but that derived window was never checked against the same
   256-element limit a named `set of` base type is held to. *)
program p; var x : integer; b : boolean;
begin b := x in [-300, 0, 5] end.

(*
CHECK: exceeds
*)
