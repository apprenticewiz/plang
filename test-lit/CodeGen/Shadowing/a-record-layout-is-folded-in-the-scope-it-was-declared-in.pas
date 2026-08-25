(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 999
*)

program p(output);
const n = 10;
type r = record a: array[1..n] of integer; tail: integer end;
procedure inner;
const n = 2;
var l: r;
begin l.a[1] := 0 end;
procedure later;
var m: r; i: integer;
begin
  m.tail := 999;
  for i := 1 to 10 do m.a[i] := i;
  writeln(m.a[10]:1, ' ', m.tail:1)
end;
begin inner; later end.
