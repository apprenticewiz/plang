(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin writeln end.

(*
CHECK:(program p
CHECK-NEXT:  (compound
CHECK-NEXT:    (call writeln)))
*)
