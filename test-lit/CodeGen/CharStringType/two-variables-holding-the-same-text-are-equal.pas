(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
*)

program p(output);
var s, t: packed array[1..3] of char;
begin s := 'abc'; t := 'abc'; writeln(s = t, ' ', s < t) end.
