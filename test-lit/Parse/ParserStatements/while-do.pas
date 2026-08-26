(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin while x > 0 do x := x - 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (while (> x 0)
CHECK-NEXT:      (assign x (- x 1)))))
*)
