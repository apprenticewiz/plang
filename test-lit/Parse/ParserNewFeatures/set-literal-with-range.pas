(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x : integer; begin if x in [1..10] then x := 0 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (if (in x [(.. 1 10)])
CHECK-NEXT:      (assign x 0))))
*)
