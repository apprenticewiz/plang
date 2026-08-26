(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var b : boolean; begin b := not not true end.

(*
CHECK:(program p
CHECK-NEXT:  (var (b) boolean)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign b (not (not true)))))
*)
