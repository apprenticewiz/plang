(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
function max(a, b : integer) : integer; forward;
function max(a, b : integer) : integer;
begin max := a end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (function max ((a b integer)) integer forward)
CHECK-NEXT:  (function max ((a b integer)) integer
CHECK-NEXT:    (compound
CHECK-NEXT:      (assign max a)))
CHECK-NEXT:  (compound))
*)
