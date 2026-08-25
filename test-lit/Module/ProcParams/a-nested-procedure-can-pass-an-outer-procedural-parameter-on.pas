(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
*)

program p;
function call2(function g(x: integer): integer; v: integer): integer;
begin call2 := g(v) end;
function ap(function f(x: integer): integer; v: integer): integer;
  function helper(y: integer): integer;
  begin helper := call2(f, y) + 1 end;
begin ap := helper(v) end;
function dbl(x: integer): integer; begin dbl := x * 2 end;
begin writeln(ap(dbl, 5)) end.
