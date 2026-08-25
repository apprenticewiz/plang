(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
var v: integer;
procedure b(var x: integer); forward;
procedure a(var x: integer); begin b(x) end;
procedure b;
begin x := 42 end;
begin v := 0; a(v); writeln(v) end.
