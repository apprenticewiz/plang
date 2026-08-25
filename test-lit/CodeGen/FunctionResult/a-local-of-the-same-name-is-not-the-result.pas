(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:local 1
CHECK-NEXT:result 2
*)

program p(output);
var n: integer;
function f: integer;
  procedure g;
  var f: integer;
  begin f := 1; writeln('local ', f:1) end;
begin g; f := 2 end;
begin n := f; writeln('result ', n:1) end.
