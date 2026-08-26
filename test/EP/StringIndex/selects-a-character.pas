(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ho
*)

program p(output); var s: string(10);
begin s := 'hello'; writeln(s[1], s[5]) end.
