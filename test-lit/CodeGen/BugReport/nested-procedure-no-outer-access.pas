(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: inner called
CHECK-DAG: 42
*)

program tnested;
procedure outer;
  var x: integer;
  procedure inner;
  begin writeln('inner called') end;
begin
  x := 42;
  inner;
  writeln(x)
end;
begin outer end.
