(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: g=42
*)

program p;
var g: integer;
procedure setIt;
begin g := 42 end;
begin
  g := 0;
  setIt;
  writeln('g=', g)
end.
