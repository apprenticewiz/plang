(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 99
*)

program p(output);
type rec = record count: integer end;
var r: rec; k: integer;
function count: integer;
begin count := 1; with r do count := 99 end;
begin r.count := 0; k := count;
  writeln(k, ' ', r.count) end.
