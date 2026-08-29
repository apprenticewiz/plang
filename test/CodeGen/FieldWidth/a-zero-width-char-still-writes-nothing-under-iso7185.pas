(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Non-regression baseline for the Turbo dialect's reversal of this rule
   (Turbo's c:0 still writes the character -- see test/Driver/Turbo): ISO
   7185 must keep writing nothing for a zero-width char field, unaffected
   by however the AlwaysWrite behavior gets threaded through to the Turbo
   side.  The sibling EP baseline for this is
   test/EP/FieldWidth/an-explicit-zero-width-is-still-unaffected.pas. *)

(*
CHECK:[]
*)

program p(output);
begin write('['); write('x':0); writeln(']') end.
