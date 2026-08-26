(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; procedure swap(var a, b : integer); begin end; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure swap ((var a b integer))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
