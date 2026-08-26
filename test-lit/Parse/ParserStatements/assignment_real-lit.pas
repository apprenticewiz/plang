(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : real; begin x := 3.14 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) real)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x 3.14)))
*)
