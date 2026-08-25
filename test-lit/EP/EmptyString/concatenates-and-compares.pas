(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[ab] true true
*)

program p(output); var s: string(10);
begin s := '' + 'ab'; writeln('[', s, '] ', s = 'ab', ' ', '' = '') end.
