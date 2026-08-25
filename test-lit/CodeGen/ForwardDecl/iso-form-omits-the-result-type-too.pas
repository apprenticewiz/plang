(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
*)

program p;
function g(n: integer): integer; forward;
function f(n: integer): integer; begin f := g(n) + 1 end;
function g;
begin g := n * 2 end;
begin writeln(f(5)) end.
