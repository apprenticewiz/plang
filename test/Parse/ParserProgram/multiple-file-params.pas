(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program rw(input, output); begin end.

(*
CHECK:(program rw
CHECK-NEXT:  (compound))
*)
