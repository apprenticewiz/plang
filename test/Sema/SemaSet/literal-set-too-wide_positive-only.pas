(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(* An untyped set literal that never gets adopted into a named set type used
   to derive its runtime bitmask window straight from its own elements with
   no width check at all -- and only bothered deriving a window in the first
   place when some element was negative, so an all-non-negative literal like
   this one kept the plain, unbounded `integer` element type and got no
   diagnostic whatsoever.  Codegen's bitmask then silently dropped 300: `x in
   [0, 300]` read as false for x = 300. *)
program p; var x : integer; b : boolean;
begin b := x in [0, 300, 5] end.

(*
CHECK: exceeds
*)
