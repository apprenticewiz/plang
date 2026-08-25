(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:q
*)

program p(output);
var c: 'a'..maxchar;
begin c := 'q'; writeln(c) end.
