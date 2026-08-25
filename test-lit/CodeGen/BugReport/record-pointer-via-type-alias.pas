(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program tptr;
type
  trec = record
    a: integer;
    b: integer;
    c: integer
  end;
  trecptr = ^trec;
var p: trecptr;
begin
  new(p);
  p^.a := 11; p^.b := 22; p^.c := 33;
  writeln(p^.a); writeln(p^.b); writeln(p^.c);
  dispose(p)
end.
