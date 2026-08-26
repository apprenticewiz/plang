(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[set]
*)

program p;
var s: string(20);
procedure setit(var x: string(20)); begin x := 'set' end;
begin s := 'orig'; setit(s); writeln('[', s, ']') end.
