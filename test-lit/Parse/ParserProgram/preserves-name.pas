(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program MyProgram; begin end.

(*
CHECK:(program MyProgram
CHECK-NEXT:  (compound))
*)
