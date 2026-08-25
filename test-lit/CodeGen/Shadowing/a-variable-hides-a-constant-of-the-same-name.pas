(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:42
CHECK-NEXT:10
*)

program p(output);
const size = 10;
procedure q;
var size: integer;
begin size := 42; writeln(size) end;
begin writeln(size); q; writeln(size) end.
