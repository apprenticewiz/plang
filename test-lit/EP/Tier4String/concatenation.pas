(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hello, World
*)

program p; var s: string(20); u: string(40);
begin s := 'Hello'; u := s + ', World'; writeln(u) end.
