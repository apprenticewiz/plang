(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type Shape = record
  case kind : integer of
    1: (radius : real);
    2: (width, height : real)
end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Shape (record (case kind : integer (1 : (radius real)) (2 : (width height real)))))
CHECK-NEXT:  (compound))
*)
