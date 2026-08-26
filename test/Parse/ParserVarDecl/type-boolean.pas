(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : boolean; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) boolean)
CHECK-NEXT:  (compound))
*)
