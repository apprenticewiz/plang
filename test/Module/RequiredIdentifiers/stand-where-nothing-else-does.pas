(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9223372036854775807 3.14159
*)

program p(output);
begin writeln(maxint, ' ', pi:0:5) end.
