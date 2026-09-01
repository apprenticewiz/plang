(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x, y : integer; begin x := 1; y := 2 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x y) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x 1)
CHECK-NEXT:    (assign y 2)))
*)
