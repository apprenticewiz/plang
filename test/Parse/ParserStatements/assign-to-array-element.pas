(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var a : array[1..5] of integer; begin a[1] := 42 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (a) (array 1 5 integer))
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign (index a 1) 42)))
*)
