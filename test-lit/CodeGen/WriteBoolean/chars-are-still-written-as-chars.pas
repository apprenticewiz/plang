(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A   A
*)

program p;
var c: char;
begin c := 'A'; writeln(c, ' ', c:3) end.
