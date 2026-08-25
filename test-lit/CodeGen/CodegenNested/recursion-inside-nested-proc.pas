(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: 1
CHECK-DAG: 3
*)

program p;
procedure outer;
  var depth: integer;
  procedure inner;
  begin
    depth := depth + 1;
    writeln(depth);
    if depth < 3 then inner
  end;
begin
  depth := 0;
  inner
end;
begin outer end.
