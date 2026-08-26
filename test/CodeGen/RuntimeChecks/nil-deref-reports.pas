(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: dereference of nil
*)

program p;
type pi = ^integer;
var q: pi;
begin q := nil; writeln(q^) end.
