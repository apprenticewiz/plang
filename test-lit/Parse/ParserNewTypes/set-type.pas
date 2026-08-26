(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type CharSet = set of char; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef CharSet (set char))
CHECK-NEXT:  (compound))
*)
