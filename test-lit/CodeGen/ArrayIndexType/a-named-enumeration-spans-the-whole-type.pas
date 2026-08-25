(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 3
*)

program p(output);
type color = (red, green, blue);
var a: array[color] of integer; c: color;
begin
  for c := red to blue do a[c] := ord(c) + 1;
  writeln(a[red], ' ', a[green], ' ', a[blue])
end.
