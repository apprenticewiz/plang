(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; const lim = 32; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const lim 32)
CHECK-NEXT:  (compound))
*)
