(*
Exit(value) checks its value the same way an ordinary `FuncName := value`
assignment to the function's own result would (isAssignCompatible against
CurrentRetType) -- a boolean value has no business in an Integer function's
result, and err_assign_mismatch, the same diagnostic an ordinary mismatched
assignment gets, says so.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot assign 'boolean' to variable of type 'integer'
*)

program p;
function F: Integer;
begin
  Exit(true)
end;
var r: Integer;
begin
  r := F
end.
