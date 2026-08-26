(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var n, x : integer;
begin repeat x := x + 1; n := n - 1 until n = 0 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (n x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (repeat
CHECK-NEXT:      (assign x (+ x 1))
CHECK-NEXT:      (assign n (- n 1))
CHECK-NEXT:      (until (= n 0)))))
*)
