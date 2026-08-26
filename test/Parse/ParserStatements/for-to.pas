(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var i : integer; begin for i := 1 to 10 do i := i + 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (i) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (for i := 1 to 10
CHECK-NEXT:      (assign i (+ i 1)))))
*)
