(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:-21
*)

program p;
function ap(function f(x: integer): integer; v: integer): integer;
begin ap := f(v) end;
function dbl(x: integer): integer; begin dbl := x * 2 end;
function neg(x: integer): integer; begin neg := -x end;
begin writeln(ap(dbl, 21)); writeln(ap(neg, 21)) end.
