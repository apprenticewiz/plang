(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
function square(x : integer) : integer;
begin square := x * x end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (function square ((x integer)) integer
CHECK-NEXT:    (compound
CHECK-NEXT:      (assign square (* x x))))
CHECK-NEXT:  (compound))
*)
