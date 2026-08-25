(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1..5: 11 22 33 44 55
CHECK-NEXT:1..5: 11 22 33 44 55
*)

program p(output);
type rec = record lo1, hi1: integer end;
var a: array[1..5] of integer; r: rec; i: integer;
procedure show(var b: array[l..h: integer] of integer);
var k: integer;
begin write(l, '..', h, ':'); for k := l to h do write(' ', b[k]);
  writeln end;
procedure relay(var b: array[lo1..hi1: integer] of integer);
begin show(b); with r do show(b) end;
begin
  for i := 1 to 5 do a[i] := i * 11;
  r.lo1 := 100; r.hi1 := 200; relay(a)
end.
