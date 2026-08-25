(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p; var s: string(20); n: integer;
begin s := 'Hello'; n := index(s, 'ell'); writeln(n) end.
