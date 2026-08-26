(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9 9
*)

program p;
type row = array[1..3] of integer;
function ramp: row;
var t: row; i: integer;
begin for i := 1 to 3 do t[i] := i * i; ramp := t end;
function head: ^integer;
var q: ^integer;
begin new(q); q^ := 9; head := q end;
begin writeln(ramp[3], ' ', head^) end.
