(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:asc
*)

program p(output); var s: string(20);
begin s := 'Pascal'; writeln(s[2..4]) end.
