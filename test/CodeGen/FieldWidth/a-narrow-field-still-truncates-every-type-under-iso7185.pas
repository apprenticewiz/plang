(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Non-regression baseline for the Turbo dialect's reversal of this rule
   (Turbo's field widths are MINIMUMS that never truncate -- see
   test/Driver/Turbo): ISO 7185 must keep truncating a Boolean or a string
   to a field narrower than the value, unaffected by however the
   non-truncating behavior gets threaded through to the Turbo side. A
   char's field width can never truncate (a char is always one character),
   so that case is pinned separately by the c:0 baseline alongside this
   file rather than repeated here. *)

(*
CHECK:[tr]
CHECK-NEXT:[fal]
CHECK-NEXT:[he]
*)

program p(output);
begin
  write('['); write(true:2);     writeln(']');
  write('['); write(false:3);    writeln(']');
  write('['); write('hello':2);  writeln(']')
end.
