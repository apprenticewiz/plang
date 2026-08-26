(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; const a = 1; b = 2; c = 3; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const a 1)
CHECK-NEXT:  (const b 2)
CHECK-NEXT:  (const c 3)
CHECK-NEXT:  (compound))
*)
