(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
function twice(function h(x: integer): integer; v: integer): integer;
begin twice := h(h(v)) end;
function useit(function apply(function q(x: integer): integer;
                              v: integer): integer;
               function f(x: integer): integer): integer;
begin useit := apply(f, 3) end;
function inc1(x: integer): integer; begin inc1 := x + 1 end;
begin writeln(useit(twice, inc1)) end.
