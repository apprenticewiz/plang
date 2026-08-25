(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:two
*)

program p;
var i: integer;
begin i := 2; case i of 1: writeln('one'); 2: writeln('two') end end.
