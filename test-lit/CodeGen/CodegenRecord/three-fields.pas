(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program p;
type Triplet = record a, b, c: integer end;
var rec: Triplet;
begin
  rec.a := 11; rec.b := 22; rec.c := 33;
  writeln(rec.a); writeln(rec.b); writeln(rec.c)
end.
