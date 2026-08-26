(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : boolean; begin x := 1 + 2 = 3 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) boolean)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x (= (+ 1 2) 3))))
*)
