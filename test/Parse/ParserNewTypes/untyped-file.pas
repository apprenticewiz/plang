(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type F = file; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef F file)
CHECK-NEXT:  (compound))
*)
