(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program calc;
var x, y, result : integer;
function max(a, b : integer) : integer;
begin
  if a > b then max := a else max := b
end;
begin
  x := 10;
  y := 20;
  result := max(x, y)
end.

(*
CHECK:(program calc
CHECK-NEXT:  (var (x y result) integer)
CHECK-NEXT:  (function max ((a b integer)) integer
CHECK-NEXT:    (compound
CHECK-NEXT:      (if (> a b)
CHECK-NEXT:        (assign max a)
CHECK-NEXT:        (assign max b))))
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign x 10)
CHECK-NEXT:    (assign y 20)
CHECK-NEXT:    (assign result (call max x y))))
*)
