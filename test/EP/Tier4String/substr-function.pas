(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ello
*)

program p; var s, u: string(20);
begin s := 'Hello'; u := substr(s, 2, 4); writeln(u) end.
