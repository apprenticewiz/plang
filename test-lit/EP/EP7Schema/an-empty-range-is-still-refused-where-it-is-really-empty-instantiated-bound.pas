(*
The suppression above is narrow on purpose, and these hold the line.
Both pass against the parent commit too: this guards the FIX from being
over-applied rather than catching the original defect, which is what a
guard is for.  Widening the suppression to every inverted bound would
pass the test above and silently accept an array with no elements.
*)

(* And a bound that does read one is checked where it is real: t(1) for a
   body of array[2..n] is 2..1 in that instantiation and refused there,
   with the instantiation's own values. *)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: lower bound 2 exceeds upper bound 1
*)

program p(output);
type t(n: integer) = array[2..n] of integer;
var v: t(1);
begin v[2] := 1; writeln('no') end.
