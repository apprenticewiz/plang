(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
*)

program p;
var c: char;
begin c := chr(200); writeln(c > 'a', ' ', c < 'a') end.
