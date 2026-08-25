(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hello
*)

program p; var s: string(20);
begin s := 'Hello'; writeln(s) end.
