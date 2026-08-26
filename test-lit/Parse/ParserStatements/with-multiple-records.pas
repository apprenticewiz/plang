(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin with r1, r2 do r1.x := 0 end.

(*
CHECK:(program p
CHECK-NEXT:  (compound
CHECK-NEXT:    (with (r1 r2)
CHECK-NEXT:      (assign (field r1 x) 0))))
*)
