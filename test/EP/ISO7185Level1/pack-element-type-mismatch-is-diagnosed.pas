(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: has component type
*)

program p;
var a: array [1..4] of integer;
    z: packed array [1..4] of char;
    i: integer;
begin i := 1; pack(a, i, z) end.
