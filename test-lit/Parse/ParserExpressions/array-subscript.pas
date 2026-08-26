(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var a : array[1..5] of integer; var x : integer; begin x := a[1] end.

(*
CHECK:(program p
CHECK-NEXT:  (var (a) (array 1 5 integer))
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x (index a 1))))
*)
