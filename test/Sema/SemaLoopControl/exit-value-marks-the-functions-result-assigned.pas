(*
Exit(value) marks the enclosing function's result assigned the same way an
ordinary `FuncName := value` does (checkAssign's own resultFrameFor) --
without that, a function whose ONLY assignment is Exit(value) on every path
would be wrongly refused err_function_no_result, since Exit is a CallStmt
and never goes through checkAssign at all.  This program's only assignment
to Abs2's result is through two Exit(value) calls, and must compile clean.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: does not assign to its result variable
*)

program p;
function Abs2(x: Integer): Integer;
begin
  if x < 0 then Exit(-x);
  Exit(x)
end;
var r: Integer;
begin
  r := Abs2(-7)
end.
