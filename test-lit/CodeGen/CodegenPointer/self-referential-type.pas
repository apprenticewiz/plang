(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
type Node = record val: integer; next: ^Node end;
var n: ^Node;
begin
  new(n); n^.val := 42; n^.next := nil;
  writeln(n^.val);
  dispose(n)
end.
