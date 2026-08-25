(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[ab][abcdefghij]
*)

program p(output); var s: string(10); u: string(3);
begin u := 'ab'; s := u; s := 'abcdefghij';
 writeln('[', u, '][', s, ']') end.
