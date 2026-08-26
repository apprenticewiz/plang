(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var s : string; begin s := 'hello' end.

(*
CHECK:(program p
CHECK-NEXT:  (var (s) string)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign s "hello")))
*)
