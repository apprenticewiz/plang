(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; type IntPtr = ^integer; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef IntPtr (^ integer))
CHECK-NEXT:  (compound))
*)
