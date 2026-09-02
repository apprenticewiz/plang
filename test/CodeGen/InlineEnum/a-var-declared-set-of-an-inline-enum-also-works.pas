(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

(* Same gap as the record-field case (issue #774), but for a directly
   var-declared set rather than one nested in a record field. *)
program p(output);
var s: set of (a, b, c);
begin
  s := [a, c];
  writeln(ord(c))
end.
