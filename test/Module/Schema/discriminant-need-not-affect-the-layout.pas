(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 8
CHECK-NEXT:5 15
*)

program p;
type tagged(id: integer) = record count: integer end;
var t: tagged(7); q: ^tagged;
procedure bump(var r: tagged);
begin r.count := r.count + r.id end;
begin
  t.count := 1; bump(t);
  writeln(t.id, ' ', t.count);
  new(q, 5); q^.count := 10; bump(q^);
  writeln(q^.id, ' ', q^.count);
  dispose(q)
end.
