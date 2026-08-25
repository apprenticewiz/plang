(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[n=42 ok]
*)

program p;
var S: string(30);
begin writestr(S, 'n=', 42, ' ok'); writeln('[', S, ']') end.
