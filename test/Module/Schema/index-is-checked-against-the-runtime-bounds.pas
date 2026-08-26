(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of bounds 1..3
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var a: vec(3);
procedure poke(var v: vec);
begin v[5] := 1 end;
begin poke(a) end.
