(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: -3  1  3
CHECK-NEXT:differ
CHECK-NEXT:same
*)

program p(output);
type a = set of -5..10; b = set of 0..10;
var x: a; y: b; i: integer;
begin
  x := [-3]; y := [1, 3];
  for i := -5 to 10 do if i in (x + y) then write(i:3);
  writeln;
  if x = y then writeln('same') else writeln('differ');
  x := [1]; y := [1];
  if x = y then writeln('same') else writeln('differ')
end.
