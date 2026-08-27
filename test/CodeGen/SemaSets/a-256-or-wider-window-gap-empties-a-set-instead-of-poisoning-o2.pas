(*
RUN: %plang -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Two compatible set types (both subranges of integer) can each fit the
   256-bit representation on their own while still being based more than 256
   ordinals apart -- alignSet used to emit their rebase shift unclamped, and
   an LLVM shift by an amount >= the operand's bit width is poison.  At -O0
   this happened to print correctly anyway; at -O2 the optimizer saw the
   poison value feed a branch condition and deleted the whole program, so it
   printed nothing at all.  No member of a's window can actually appear in
   b's once the two origins are 256 or more apart, so the correctly rebased
   result is the empty set. *)
program p(output);
type a = set of -300..-250; b = set of 0..10;
var x: a; y: b;
begin
  x := [-300, -260];
  y := x;
  if y = [] then writeln('ok') else writeln('bad')
end.

(*
CHECK:ok
*)
