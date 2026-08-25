(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
var q: ^integer;
begin
  new(q);
  q^ := 42;
  writeln(q^);
  dispose(q)
end.
