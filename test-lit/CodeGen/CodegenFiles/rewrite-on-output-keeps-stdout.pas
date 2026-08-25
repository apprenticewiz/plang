(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:still stdout
*)

program p(output);
begin rewrite(output); writeln(output, 'still stdout') end.
