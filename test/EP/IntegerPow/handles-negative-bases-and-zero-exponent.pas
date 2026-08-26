(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-27 9 1
*)

program p(output);
begin writeln((-3) pow 3, ' ', (-3) pow 2, ' ', 5 pow 0) end.
