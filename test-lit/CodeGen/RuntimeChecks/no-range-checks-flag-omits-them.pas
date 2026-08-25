(*
RUN: %plang -fno-range-checks %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:no check
*)

program p;
var s: 1..10; i: integer;
begin i := 500; s := i; writeln('no check') end.
