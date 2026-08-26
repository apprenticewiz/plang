(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin with rec do rec.x := 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (compound
CHECK-NEXT:    (with (rec)
CHECK-NEXT:      (assign (field rec x) 1))))
*)
