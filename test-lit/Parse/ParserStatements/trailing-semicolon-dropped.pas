(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin x := 1; end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x 1)))
*)
