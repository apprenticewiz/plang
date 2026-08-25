(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ok
*)

program p;
var s: string(20);
procedure w(var t: string(20));
begin t := 'ok' end;
begin w(s); writeln(s) end.
