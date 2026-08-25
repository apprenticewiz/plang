(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK-DAG: seen=7
*)

program p;
var g: integer;
procedure showIt;
begin writeln('seen=', g) end;
begin
  g := 7;
  showIt
end.
