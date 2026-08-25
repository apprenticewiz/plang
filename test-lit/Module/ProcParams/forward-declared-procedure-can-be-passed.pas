(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
function later(x: integer): integer; forward;
function later(x: integer): integer; begin later := x + 9 end;
begin writeln(ap(later, 1)) end.
