(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Non-regression baseline for the Turbo dialect's reversal of this rule
   (Turbo writes UPPERCASE TRUE/FALSE -- see test/Driver/Turbo): ISO 7185
   itself must keep writing lowercase true/false, unaffected by however the
   spelling gets threaded through to the Turbo side. *)

(*
CHECK:true
CHECK-NEXT:false
*)

program p(output);
begin write(true); writeln; write(false); writeln end.
