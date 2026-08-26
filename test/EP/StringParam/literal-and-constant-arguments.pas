(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi]
CHECK-NEXT:[hello]
*)

program p;
const g = 'hello';
procedure show(x: string(20)); begin writeln('[', x, ']') end;
begin show('hi'); show(g) end.
