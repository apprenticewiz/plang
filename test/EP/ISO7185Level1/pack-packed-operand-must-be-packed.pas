(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: packed argument must be packed
*)

program p;
var a: array [1..4] of integer;
    z: array [1..4] of integer;
    i: integer;
begin i := 1; pack(a, i, z) end.
