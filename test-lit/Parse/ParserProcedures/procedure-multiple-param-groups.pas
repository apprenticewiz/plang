(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; procedure f(x : integer; y : real); begin end; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure f ((x integer) (y real))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
