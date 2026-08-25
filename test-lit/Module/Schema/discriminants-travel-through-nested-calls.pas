(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 3
CHECK-NEXT:5 10
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var p1, p2: ^vec; i: integer;
function sum(var v: vec): integer;
var j, s: integer;
begin s := 0; for j := 1 to v.n do s := s + v[j]; sum := s end;
procedure report(var v: vec);
  procedure inner(var w: vec);
  begin writeln(w.n, ' ', sum(w)) end;
begin inner(v) end;
begin
  new(p1, 3); new(p2, 5);
  for i := 1 to 3 do p1^[i] := 1;
  for i := 1 to 5 do p2^[i] := 2;
  report(p1^); report(p2^);
  dispose(p1); dispose(p2)
end.
