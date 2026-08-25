(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:24
*)

program p;
type c = (red, green, blue);
var a: array[-3..-1] of integer;
    b: array['a'..'e'] of integer;
    d: array[red..blue] of integer;
begin
  a[-2] := 7; b['c'] := 8; d[green] := 9;
  writeln(a[-2] + b['c'] + d[green])
end.
