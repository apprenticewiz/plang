(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

program p(output);
type big = array[1..100000] of integer;
var a: big; i: integer;
function down(k: integer; x: array[lo..hi: integer] of integer): integer;
begin if k = 0 then down := x[lo] else down := down(k - 1, x) end;
begin for i := 1 to 100000 do a[i] := i; writeln(down(20, a)) end.
