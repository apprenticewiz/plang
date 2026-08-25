(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

program p;
var v: integer;
procedure show(x: type of v);
begin writeln(x) end;
begin show(99) end.
