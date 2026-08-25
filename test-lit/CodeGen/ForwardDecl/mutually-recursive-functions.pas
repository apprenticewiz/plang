(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true true
*)

program p;
function isodd(n: integer): boolean; forward;
function iseven(n: integer): boolean;
begin if n = 0 then iseven := true else iseven := isodd(n - 1) end;
function isodd(n: integer): boolean;
begin if n = 0 then isodd := false else isodd := iseven(n - 1) end;
begin writeln(iseven(10), ' ', isodd(7)) end.
