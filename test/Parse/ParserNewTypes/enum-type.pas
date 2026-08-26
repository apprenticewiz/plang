(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Color = (red, green, blue); begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Color (enum red green blue))
CHECK-NEXT:  (compound))
*)
