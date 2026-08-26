(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type IntFile = file of integer; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef IntFile (file integer))
CHECK-NEXT:  (compound))
*)
