(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: non-positive divisor -3
*)

program p;
var a, b: integer;
begin a := 10; b := -3; writeln(a mod b) end.
