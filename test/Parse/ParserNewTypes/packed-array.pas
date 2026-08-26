(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Str = packed array[1..80] of char; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Str (packed-array 1 80 char))
CHECK-NEXT:  (compound))
*)
