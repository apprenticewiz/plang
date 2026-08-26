(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type Small = minVal..maxVal; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Small (subrange minVal maxVal))
CHECK-NEXT:  (compound))
*)
