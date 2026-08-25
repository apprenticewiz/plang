(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
*)

program p;
type PNode = ^Node;
     Node  = record val: integer end;
var n: PNode;
begin
  new(n); n^.val := 99; writeln(n^.val); dispose(n)
end.
