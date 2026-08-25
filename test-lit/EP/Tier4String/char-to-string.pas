(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:!
*)

program p; var s: string(10); c: char;
begin c := '!'; s := c; writeln(s) end.
