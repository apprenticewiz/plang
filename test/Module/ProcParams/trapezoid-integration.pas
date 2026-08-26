(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9.0000
CHECK-NEXT:16.0000
*)

program p;
function integrate(function f(x: real): real;
                   a, b: real; n: integer): real;
var i: integer; h, s: real;
begin
  h := (b - a) / n; s := (f(a) + f(b)) / 2.0;
  for i := 1 to n - 1 do s := s + f(a + i * h);
  integrate := s * h
end;
function sq(x: real): real; begin sq := x * x end;
function lin(x: real): real; begin lin := 2.0 * x end;
begin
  writeln(integrate(sq, 0.0, 3.0, 3000):0:4);
  writeln(integrate(lin, 0.0, 4.0, 3000):0:4)
end.
