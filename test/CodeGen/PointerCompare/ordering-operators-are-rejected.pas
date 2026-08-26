(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not defined for pointers
*)

program p;
type pi = ^integer;
var a, b: pi;
begin a := nil; b := nil; writeln(a < b) end.
