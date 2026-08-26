(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var a, b : boolean; var x : integer;
begin if a then if b then x := 1 else x := 2 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (a b) boolean)
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (if a
CHECK-NEXT:      (if b
CHECK-NEXT:        (assign x 1)
CHECK-NEXT:        (assign x 2)))))
*)
