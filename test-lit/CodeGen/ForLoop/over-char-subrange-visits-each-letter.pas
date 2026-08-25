(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcde
*)

program p;
var c: char;
begin for c := 'a' to 'e' do write(c); writeln end.
