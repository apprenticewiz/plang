(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program graph1(output); begin end.

(*
CHECK:(program graph1
CHECK-NEXT:  (compound))
*)
