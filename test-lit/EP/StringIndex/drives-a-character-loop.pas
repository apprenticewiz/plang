(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:h-e-l-l-o-
*)

program p(output); var s: string(10); i: integer;
begin s := 'hello';
 for i := 1 to length(s) do write(s[i], '-'); writeln end.
