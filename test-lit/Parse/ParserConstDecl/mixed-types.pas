(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; const n = 10; x = 3.14; s = 'hi'; b = true; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const n 10)
CHECK-NEXT:  (const x 3.14)
CHECK-NEXT:  (const s "hi")
CHECK-NEXT:  (const b true)
CHECK-NEXT:  (compound))
*)
