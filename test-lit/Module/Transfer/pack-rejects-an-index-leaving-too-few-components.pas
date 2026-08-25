(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: out of bounds
*)

program p;
var a: array[1..10] of integer;
    z: packed array[1..4] of integer;
    i: integer;
begin
  for i := 1 to 10 do a[i] := i;
  pack(a, 9, z);
  writeln('unreachable')
end.
