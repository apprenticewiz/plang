(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type Node = record value : integer; next : ^Node end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Node (record (value integer) (next (^ Node))))
CHECK-NEXT:  (compound))
*)
