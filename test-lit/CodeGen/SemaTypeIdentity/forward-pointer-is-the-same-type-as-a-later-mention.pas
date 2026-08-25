(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p;
type pnode = ^node;
     node = record v: integer; next: ^node end;
var head: pnode; other: ^node;
begin
  new(head); head^.v := 3; head^.next := nil;
  other := head; writeln(other^.v)
end.
