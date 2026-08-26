(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type MyInt = integer; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef MyInt integer)
CHECK-NEXT:  (compound))
*)
