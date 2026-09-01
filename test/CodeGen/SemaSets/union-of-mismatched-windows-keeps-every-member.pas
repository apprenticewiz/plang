(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Issue #681: '+' typed its result from whichever operand happened to be on
   the LEFT regardless of whether that operand's own window actually covered
   the other's.  Here x's own base (0) sits ABOVE y's (-5), so the old code
   picked x's window as the result's, and codegen right-shifted y's bits into
   it -- dropping every y-only member below x's own base instead of widening
   to fit both, the same silent-corruption class as the comparison operators'
   own #225/#226 fixes, but in the value-producing operators themselves.
   Loops the full merged span so both edges are checked at once, not just
   the issue's own repro value: x's own extremes (0, 10) and y's own (-5,
   -4 -- the issue's own vanishing member -- and 5), every one of which a
   window-alignment bug could drop independently of the others. *)

program p(output);
type a = set of 0..10; b = set of -5..5;
var x: a; y: b; i: integer;
begin
  x := [0, 3, 5, 10];
  y := [-5, -4, 2];
  for i := -5 to 10 do if i in (x + y) then write(i:3);
  writeln
end.

(*
CHECK: -5 -4  0  2  3  5 10
*)
