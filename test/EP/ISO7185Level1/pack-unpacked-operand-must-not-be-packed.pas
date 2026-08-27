(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: unpacked argument must not be packed
*)

program p;
var a: packed array [1..4] of integer;
    z: packed array [1..4] of integer;
    i: integer;
begin i := 1; pack(a, i, z) end.
