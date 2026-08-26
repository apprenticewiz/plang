(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; var i : integer; begin for i := 10 downto 1 do i := i - 1 end.

(*
CHECK:(program p
CHECK-NEXT:  (var (i) integer)
CHECK-NEXT:  (compound
CHECK-NEXT:    (for i := 10 downto 1
CHECK-NEXT:      (assign i (- i 1)))))
*)
