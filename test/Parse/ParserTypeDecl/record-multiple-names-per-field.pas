(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type Rect = record left, right : integer; top, bottom : integer end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Rect (record (left right integer) (top bottom integer)))
CHECK-NEXT:  (compound))
*)
