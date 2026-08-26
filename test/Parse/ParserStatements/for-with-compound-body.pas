(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var i, s : integer;
begin for i := 1 to 5 do begin s := s + i end end.

(*
CHECK:(program p
CHECK-NEXT:  (var (i s) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (for i := 1 to 5
CHECK-NEXT:      (compound
CHECK-NEXT:        (assign s (+ s i))))))
*)
