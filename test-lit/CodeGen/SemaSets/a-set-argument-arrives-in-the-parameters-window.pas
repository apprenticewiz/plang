(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  1  3
*)

program p(output);
type a = set of -5..10; b = set of 0..10;
var y: b;
procedure show(s: a);
var j: integer;
begin
  for j := -5 to 10 do if j in s then write(j:3);
  writeln
end;
begin y := [1, 3]; show(y) end.
