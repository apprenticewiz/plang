(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:empty
*)

program p(input, output);
begin if eof then writeln('empty') else writeln('not') end.
