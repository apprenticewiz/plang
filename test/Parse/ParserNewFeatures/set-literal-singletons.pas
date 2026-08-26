(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin x := 0; if x in [1, 2, 3] then x := 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x 0)
CHECK-NEXT:    (if (in x [1 2 3])
CHECK-NEXT:      (assign x 1))))
*)
