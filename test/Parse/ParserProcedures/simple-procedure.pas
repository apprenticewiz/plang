(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; procedure foo; begin end; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure foo ()
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
