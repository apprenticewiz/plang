(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of bounds 97..99
*)

program p;
var a: array['a'..'c'] of integer;
begin a['z'] := 1 end.
