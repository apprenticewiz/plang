(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 0
*)

program p(output);
type tagged(id: integer) = record count: integer end;
var q: ^tagged;
begin new(q, 0); q^.count := 7; writeln(q^.count:1, ' ', q^.id:1) end.
