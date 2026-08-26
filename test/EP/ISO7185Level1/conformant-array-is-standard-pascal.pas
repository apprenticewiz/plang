(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p(output);
var a: array [1..4] of integer; i: integer;
function total(x: array [lo..hi: integer] of integer): integer;
var j, s: integer;
begin s := 0; for j := lo to hi do s := s + x[j]; total := s end;
begin for i := 1 to 4 do a[i] := i; writeln(total(a)) end.
