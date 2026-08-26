(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=[init] len=4
*)

program p;
type st = string(12);
var a: st value 'init';
begin writeln('a=[', a, '] len=', length(a)) end.
