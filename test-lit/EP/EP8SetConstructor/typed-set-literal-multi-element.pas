(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:red
CHECK-NEXT:green
CHECK-NEXT:no blue
*)

program p;
type Color = (red, green, blue);
type Colors = set of Color;
var c: Colors;
begin
  c := Colors[red, green];
  if red in c then writeln('red');
  if green in c then writeln('green');
  if not (blue in c) then writeln('no blue')
end.
