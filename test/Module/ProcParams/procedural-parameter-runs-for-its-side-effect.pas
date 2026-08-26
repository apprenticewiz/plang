(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:15
*)

program p;
var total: integer;
procedure add(x: integer); begin total := total + x end;
procedure thrice(procedure act(x: integer); v: integer);
begin act(v); act(v); act(v) end;
begin total := 0; thrice(add, 5); writeln(total) end.
