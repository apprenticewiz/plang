(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot compare
*)

program p;
type pi = ^integer;
var a: pi; n: integer;
begin a := nil; n := 0; writeln(a = n) end.
