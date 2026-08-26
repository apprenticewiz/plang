(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[xab]
*)

program p(output); var c: char; s: string(10);
begin c := 'x'; s := 'ab'; s := c + s; writeln('[', s, ']') end.
