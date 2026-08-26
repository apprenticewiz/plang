(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type BS = packed set of boolean; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef BS (packed-set boolean))
CHECK-NEXT:  (compound))
*)
