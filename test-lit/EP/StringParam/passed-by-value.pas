(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi]
*)

program p;
var s: string(20);
procedure show(x: string(20)); begin writeln('[', x, ']') end;
begin s := 'hi'; show(s) end.
