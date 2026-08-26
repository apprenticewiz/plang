(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
function double(x : integer) : integer;
begin double := x * 2 end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (function double ((x integer)) integer
CHECK-NEXT:    (compound
CHECK-NEXT:      (assign double (* x 2))))
CHECK-NEXT:  (compound))
*)
