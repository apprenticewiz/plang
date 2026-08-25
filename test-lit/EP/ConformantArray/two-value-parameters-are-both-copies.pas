(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:123 102030
*)

program p(output);
type vec = array[1..3] of integer;
var a, b: vec; i: integer;
procedure two(p: array[l1..h1: integer] of integer;
              q: array[l2..h2: integer] of integer);
var k: integer;
begin for k := l1 to h1 do p[k] := 0;
  for k := l2 to h2 do q[k] := 0 end;
begin
  for i := 1 to 3 do begin a[i] := i; b[i] := i * 10 end;
  two(a, b);
  write(a[1], a[2], a[3], ' ', b[1], b[2], b[3]); writeln
end.
