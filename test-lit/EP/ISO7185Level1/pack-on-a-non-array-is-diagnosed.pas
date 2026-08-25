(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: expects an array
*)

program p;
var i: integer; z: packed array [1..3] of char;
begin pack(i, 1, z) end.
