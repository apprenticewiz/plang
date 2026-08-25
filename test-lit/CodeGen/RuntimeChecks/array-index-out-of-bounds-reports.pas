(*
RUN: %plang %s -o %t
RUN: not %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: array index 9 out of bounds 1..5
*)

program p;
var a: array[1..5] of integer; i: integer;
begin
  for i := 1 to 5 do a[i] := i;
  i := 9; writeln(a[i])
end.
