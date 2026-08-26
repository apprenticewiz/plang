(*
A schema body is resolved once with its discriminants bound to 1, to get
its element and field TYPES; its extents are the probe's and are marked
ExtentVaries so nothing uses them.  Diagnosing them was the one thing
that did, and it rejected legal programs: `array[2..n]` folds to 2..1 at
the probe, `array[1..n-1]` to 1..0, and the message quoted bounds the
program never wrote.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p(output);
type t(n: integer) = record a: array[2..n] of integer end;
var q: ^t;
begin new(q, 5); q^.a[2] := 7; writeln(q^.a[2]:1) end.
