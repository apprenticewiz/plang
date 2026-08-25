(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p;
function d(x: integer): integer; begin d := x * 100 end;
function ap(function d(x: integer): integer): integer;
begin ap := d(2) end;
function e(x: integer): integer; begin e := x + 1 end;
begin writeln(ap(e)) end.
