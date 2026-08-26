(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type R = packed record x : integer end; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef R (packed-record (x integer)))
CHECK-NEXT:  (compound))
*)
