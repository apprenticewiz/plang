(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var p : integer; begin p := nil end.

(*
CHECK:(program p
CHECK-NEXT:  (var (p) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign p nil)))
*)
