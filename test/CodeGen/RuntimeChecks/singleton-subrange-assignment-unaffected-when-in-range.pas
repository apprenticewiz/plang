(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

(*
The one value a singleton subrange 5..5 can legally hold must still pass the
range check unimpeded.
*)

program p;
var x: 5..5; n: integer;
begin
  n := 5; x := n;
  writeln(x:1)
end.
