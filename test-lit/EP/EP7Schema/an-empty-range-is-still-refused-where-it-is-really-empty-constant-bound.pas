(*
The suppression above is narrow on purpose, and these hold the line.
Both pass against the parent commit too: this guards the FIX from being
over-applied rather than catching the original defect, which is what a
guard is for.  Widening the suppression to every inverted bound would
pass the test above and silently accept an array with no elements.
*)

(* A bound that reads NO discriminant is the same in every instantiation,
   so the probe's numbers are the program's and array[5..2] is still
   refused inside a schema body. *)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: lower bound 5 exceeds upper bound 2
*)

program p(output);
type t(n: integer) = record a: array[5..2] of integer end;
var q: ^t;
begin new(q, 9); writeln('no') end.
