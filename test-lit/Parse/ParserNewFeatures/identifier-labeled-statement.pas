(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin done: x := 0 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (label done
CHECK-NEXT:      (assign x 0))))
*)
