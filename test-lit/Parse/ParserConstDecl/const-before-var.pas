(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; const n = 1; var x : integer; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const n 1)
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound))
*)
