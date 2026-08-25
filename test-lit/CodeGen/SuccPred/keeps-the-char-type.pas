(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:b
CHECK-NEXT:y
*)

program p;
var c: char;
begin c := succ('a'); writeln(c); c := pred('z'); writeln(c) end.
