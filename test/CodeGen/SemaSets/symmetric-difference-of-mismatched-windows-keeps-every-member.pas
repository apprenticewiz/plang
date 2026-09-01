(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Issue #681: '><' (EP §6.8.3.4 symmetric difference) had the identical
   left-operand-only bug '+' has (see union-of-mismatched-windows-keeps-
   every-member.pas) -- unsurprising, since A><B is (A∪B)-(A∩B), a union
   under the hood, and a member only h has, below g's own base, was
   silently dropped the same way.  g and h share member 3, which must
   NOT survive into the symmetric difference, so this also checks that
   widening the result's window didn't turn the XOR into a plain union. *)

program p(output);
type a = set of 0..10; b = set of -5..5;
var g: a; h: b; i: integer;
begin
  g := [0, 3, 5, 10];
  h := [-5, -4, 3];
  for i := -5 to 10 do if i in (g >< h) then write(i:3);
  writeln
end.

(*
CHECK: -5 -4  0  5 10
*)
