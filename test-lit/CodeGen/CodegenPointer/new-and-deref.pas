(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program p;
type Rec = record a, b, c: integer end;
var r: ^Rec;
begin
  new(r);
  r^.a := 11; r^.b := 22; r^.c := 33;
  writeln(r^.a); writeln(r^.b); writeln(r^.c);
  dispose(r)
end.
