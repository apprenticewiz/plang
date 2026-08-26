(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p;
var hits: integer;
procedure ping; begin hits := hits + 1 end;
procedure run(procedure a); begin a; a end;
begin hits := 0; run(ping); writeln(hits) end.
