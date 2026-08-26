(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Grid = array[0..99] of MyType; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Grid (array 0 99 MyType))
CHECK-NEXT:  (compound))
*)
