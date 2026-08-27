(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(* ISO §6.9.3.1: the decimals after the second colon is an integer-expression
   too, just like width above (issue #256: write(x:3:'x') used to silently
   widen 'x' to 120 decimal places, no diagnostic).  A valid integer width
   (3) precedes it, so this isolates the decimals check from the width one. *)
(*
ERR: write field-decimals must be an integer expression, got 'char'
*)

program p(output);
begin writeln(1.5: 3: 'x') end.
