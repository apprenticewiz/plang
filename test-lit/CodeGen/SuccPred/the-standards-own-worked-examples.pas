(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0
CHECK-NEXT:0
CHECK-NEXT:2
CHECK-NEXT:3
CHECK-NEXT:1
CHECK-NEXT:0
CHECK-NEXT:1
CHECK-NEXT:1
*)

program p(output);
type shape = (triangle, square, pentagon);
     colour = (red, yellow, green, blue);
var c: colour; s: shape;
begin
  c := succ(yellow, -1); writeln(ord(c):1);
  s := succ(triangle, 0); writeln(ord(s):1);
  c := succ(yellow); writeln(ord(c):1);
  c := succ(yellow, 2); writeln(ord(c):1);
  c := pred(red, -1); writeln(ord(c):1);
  s := pred(triangle, 0); writeln(ord(s):1);
  c := pred(green); writeln(ord(c):1);
  c := pred(blue, 2); writeln(ord(c):1)
end.
