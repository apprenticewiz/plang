(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-DAG: inner ran
CHECK-DAG: outer x=101
*)

program tnested;
var g: integer;
procedure outer;
var x: integer;
  procedure inner;
  begin x := x + 100; writeln('inner ran, x=', x) end;
begin
  x := 1; inner; writeln('outer x=', x)
end;
begin g := 0; outer; writeln('done') end.
