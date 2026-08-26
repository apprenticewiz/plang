(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
procedure foo(x : integer); forward;
procedure foo(x : integer);
begin end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (procedure foo ((x integer)) forward)
CHECK-NEXT:  (procedure foo ((x integer))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
