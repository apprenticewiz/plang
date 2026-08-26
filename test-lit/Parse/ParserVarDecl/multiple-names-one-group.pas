(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x, y, z : real; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x y z) real)
CHECK-NEXT:  (compound))
*)
