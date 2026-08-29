(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Non-regression baseline for the Turbo dialect's reversal of this rule
   (Turbo's field widths are MINIMUMS that never truncate -- see
   test/Driver/Turbo): Extended Pascal must keep truncating a Boolean or a
   string to a field narrower than the value, unaffected by however the
   non-truncating behavior gets threaded through to the Turbo side. *)

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
