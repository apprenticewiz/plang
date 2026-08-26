(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:7
*)

program p;
function countElems(A: array [lo..hi : integer] of integer) : integer;
begin
  countElems := hi - lo + 1
end;
var a3: array [1..3] of integer;
var a7: array [1..7] of integer;
begin
  writeln(countElems(a3));
  writeln(countElems(a7))
end.
