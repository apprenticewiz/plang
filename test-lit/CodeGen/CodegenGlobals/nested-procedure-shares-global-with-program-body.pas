(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: g=101
*)

program p;
var g: integer;
procedure outer;
  procedure inner;
  begin g := g + 100 end;
begin inner end;
begin
  g := 1;
  outer;
  writeln('g=', g)
end.
