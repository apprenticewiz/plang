(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var x, y : integer;
begin while x > 0 do begin y := y + x; x := x - 1 end end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x y) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (while (> x 0)
CHECK-NEXT:      (compound
CHECK-NEXT:        (assign y (+ y x))
CHECK-NEXT:        (assign x (- x 1))))))
*)
