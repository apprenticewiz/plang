(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:1
*)

program p;
type col = (red, green, blue);
var c: col;
begin c := succ(red); writeln(ord(c));
 c := pred(blue); writeln(ord(c)) end.
