(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type Tagged = record
  name : string;
  case t : boolean of
    true: (x : integer);
    false: (y : real)
end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Tagged (record (name string) (case t : boolean (true : (x integer)) (false : (y real)))))
CHECK-NEXT:  (compound))
*)
