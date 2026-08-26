(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
procedure outer;
  procedure inner;
  begin end;
begin end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure outer ()
CHECK-NEXT:    (procedure inner ()
CHECK-NEXT:      (compound))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
