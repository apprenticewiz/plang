(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type A = integer; B = real; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef A integer)
CHECK-NEXT:  (typedef B real)
CHECK-NEXT:  (compound))
*)
