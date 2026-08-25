(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true true false
*)

program p;
var s: set of char;
begin
  s := ['a', 'b', 'z'];
  writeln('a' in s, ' ', 'z' in s, ' ', 'q' in s)
end.
