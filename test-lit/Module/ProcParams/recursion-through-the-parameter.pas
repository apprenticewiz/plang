(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:720
*)

program p;
function apply(function f(x: integer): integer; v: integer): integer;
begin apply := f(v) end;
function fact(n: integer): integer;
begin if n <= 1 then fact := 1 else fact := n * apply(fact, n - 1) end;
begin writeln(apply(fact, 6)) end.
