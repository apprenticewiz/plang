(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer 1
CHECK-NEXT:inner 2
CHECK-NEXT:outer 1
*)

program p;
const c = 1;
procedure q; const c = 2; begin writeln('inner ', c) end;
begin writeln('outer ', c); q; writeln('outer ', c) end.
