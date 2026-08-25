(*
RUN: %plang %s -o %t
RUN: not %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 500 out of range 1..10
*)

program p;
var s: 1..10; i: integer;
begin i := 500; s := i; writeln(s) end.
