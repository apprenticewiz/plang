(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p; var s: string(20); n: integer;
begin s := 'Hello'; n := length(s); writeln(n) end.
