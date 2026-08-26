(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var x : integer;
begin
  x := 1 * 2;
  x := 1 / 2;
  x := 1 div 2;
  x := 1 mod 2;
  x := 1 and 2
end.

(*
CHECK:(program p
CHECK-NEXT:  (var (x) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x (* 1 2))
CHECK-NEXT:    (assign x (/ 1 2))
CHECK-NEXT:    (assign x (div 1 2))
CHECK-NEXT:    (assign x (mod 1 2))
CHECK-NEXT:    (assign x (and 1 2))))
*)
