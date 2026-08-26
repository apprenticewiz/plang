(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:35
*)

program p;
function combine(function f(x: integer): integer;
                 function g(x: integer): integer; v: integer): integer;
begin combine := f(v) + g(v) end;
function dbl(x: integer): integer; begin dbl := x * 2 end;
function sq(x: integer): integer; begin sq := x * x end;
begin writeln(combine(dbl, sq, 5)) end.
