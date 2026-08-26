(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[] 0
*)

program p(output); var s: string(10);
begin s := ''; writeln('[', s, '] ', length(s)) end.
