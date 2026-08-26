(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Digit = 0..9; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Digit (subrange 0 9))
CHECK-NEXT:  (compound))
*)
