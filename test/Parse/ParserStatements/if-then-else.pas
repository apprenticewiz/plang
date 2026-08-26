(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin if x > 0 then x := 1 else x := 0 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (if (> x 0)
CHECK-NEXT:      (assign x 1)
CHECK-NEXT:      (assign x 0))))
*)
