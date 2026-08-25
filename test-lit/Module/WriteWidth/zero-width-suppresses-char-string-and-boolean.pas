(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[][][]
CHECK-NEXT:[  X][  true][  hi]
*)

program p;
var c: char; b: boolean;
begin
  c := 'X'; b := true;
  writeln('[', c:0, '][', b:0, '][', 'hi':0, ']');
  writeln('[', c:3, '][', b:6, '][', 'hi':4, ']')
end.
