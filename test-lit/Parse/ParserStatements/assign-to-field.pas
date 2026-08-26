(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin r.x := 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign (field r x) 1)))
*)
