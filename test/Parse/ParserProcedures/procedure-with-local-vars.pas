(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
procedure f;
  var tmp : integer;
begin end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure f ()
CHECK-NEXT:    (var (tmp) integer)
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
