(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type Point = record x : real; y : real end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Point (record (x real) (y real)))
CHECK-NEXT:  (compound))
*)
