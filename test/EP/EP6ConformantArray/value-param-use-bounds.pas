(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:7
*)

program p;
procedure showbounds(A: array [lo..hi : integer] of integer);
begin
  writeln(lo);
  writeln(hi)
end;
var arr: array [3..7] of integer;
begin
  showbounds(arr)
end.
