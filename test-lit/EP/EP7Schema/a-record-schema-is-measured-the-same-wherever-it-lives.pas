(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:303 33 202 22
CHECK-NEXT:21 77
*)

program p(output);
type box(n: integer) = record w: array[0..n] of integer; tail: integer end;
type pbox = ^box(3);
var ptr: pbox; i: integer;
procedure local;
var l3: box(3); l2: box(2); j: integer;
begin
  for j := 0 to 3 do l3.w[j] := 300 + j;
  for j := 0 to 2 do l2.w[j] := 200 + j;
  l3.tail := 33; l2.tail := 22;
  writeln(l3.w[3]:0, ' ', l3.tail:0, ' ', l2.w[2]:0, ' ', l2.tail:0)
end;
begin
  local;
  new(ptr);
  for i := 0 to 3 do ptr^.w[i] := i * 7;
  ptr^.tail := 77;
  writeln(ptr^.w[3]:0, ' ', ptr^.tail:0)
end.
