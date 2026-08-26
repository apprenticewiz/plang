(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi] 2
*)

program p;
var s: string(10) value 'hi';
begin writeln('[', s, '] ', length(s)) end.
