(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
*)

program p;
type ListNode = record val: integer; next: ^ListNode end;
var head, tail: ^ListNode;
begin
  new(head); head^.val := 1; head^.next := nil;
  new(tail); tail^.val := 2; tail^.next := nil;
  head^.next := tail;
  writeln(head^.val);
  writeln(head^.next^.val)
end.
