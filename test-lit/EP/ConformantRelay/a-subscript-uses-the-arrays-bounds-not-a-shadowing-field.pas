(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:50 90
CHECK-NEXT:50 90
*)

program p(output);
type rec = record lo, hi: integer end;
var a: array[5..9] of integer; r: rec; i: integer;
procedure show(x: array[lo..hi: integer] of integer);
begin
  writeln(x[5], ' ', x[9]);
  with r do writeln(x[5], ' ', x[9])
end;
begin
  for i := 5 to 9 do a[i] := i * 10;
  r.lo := 0; r.hi := 0; show(a)
end.
