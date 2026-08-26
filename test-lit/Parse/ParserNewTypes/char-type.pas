(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var c : char; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (var (c) char)
CHECK-NEXT:  (compound))
*)
