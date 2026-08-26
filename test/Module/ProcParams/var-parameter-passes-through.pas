(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
*)

program p;
var k: integer;
procedure bump(var x: integer); begin x := x + 10 end;
procedure apply(procedure f(var y: integer); var n: integer);
begin f(n) end;
begin k := 1; apply(bump, k); writeln(k) end.
