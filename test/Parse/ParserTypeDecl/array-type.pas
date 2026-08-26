(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Vec = array[1..10] of real; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Vec (array 1 10 real))
CHECK-NEXT:  (compound))
*)
