(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[ 42  ]
*)

program p(output); var c: packed array[1..5] of char;
begin writestr(c, 42:3); writeln('[', c, ']') end.
