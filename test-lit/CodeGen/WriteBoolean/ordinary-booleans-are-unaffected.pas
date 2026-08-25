(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false   true
*)

program p;
type r = record flag: boolean end;
var b: boolean; x: r;
begin b := true; x.flag := false;
  writeln(b, ' ', x.flag, ' ', b:6) end.
