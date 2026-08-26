(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x, y : integer; begin y := x end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x y) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign y x)))
*)
