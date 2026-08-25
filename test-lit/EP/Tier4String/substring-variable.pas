(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
*)

program p; var s: string(20); n: integer;
begin s := 'Pascal'; n := length(s[2..4]); writeln(n) end.
