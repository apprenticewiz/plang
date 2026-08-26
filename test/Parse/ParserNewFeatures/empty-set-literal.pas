(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var s : integer; begin s := 0; if 1 in [] then s := 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (s) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign s 0)
CHECK-NEXT:    (if (in 1 [])
CHECK-NEXT:      (assign s 1))))
*)
