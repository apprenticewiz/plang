(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:100
CHECK-NEXT:200
*)

program p;
type Pair = record first, second: integer end;
var q: ^Pair;
begin
  new(q);
  q^.first := 100; q^.second := 200;
  writeln(q^.first);
  writeln(q^.second);
  dispose(q)
end.
