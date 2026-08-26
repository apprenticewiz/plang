(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
*)

program p; var s: string(20); b: boolean;
begin s := 'Apple'; b := s < 'Banana'; writeln(b) end.
