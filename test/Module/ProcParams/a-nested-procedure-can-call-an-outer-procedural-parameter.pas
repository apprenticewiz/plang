(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:60
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
  function helper(y: integer): integer;
  begin helper := f(y) * 10 end;
begin ap := helper(v) end;
function dbl(x: integer): integer; begin dbl := x * 2 end;
begin writeln(ap(dbl, 3)) end.
