(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:7
CHECK-NEXT:8
*)

program p;
var g: integer;
function pick: integer;
begin pick := 7 end;
procedure show(v: integer); begin writeln(v) end;
begin g := pick; writeln(g); show(pick); writeln(pick + 1) end.
