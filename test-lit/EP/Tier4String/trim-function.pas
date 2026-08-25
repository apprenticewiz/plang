(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p; var s, u: string(20); n: integer;
begin s := 'hello   '; u := trim(s); n := length(u); writeln(n) end.
