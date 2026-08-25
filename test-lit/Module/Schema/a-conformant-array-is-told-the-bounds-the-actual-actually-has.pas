(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bounds 1..30
CHECK-NEXT:last=9
*)

program p(output);
const hi = 10;
type vec(n: integer) = array[1..n*hi] of integer;
var v: ^vec;
procedure show(var a: array[lo..h: integer] of integer);
begin
  writeln('bounds ', lo:1, '..', h:1);
  a[h] := 9; writeln('last=', a[h]:1)
end;
procedure caller;
var hi: integer;
begin hi := 2; show(v^) end;
begin new(v, 3); caller end.
