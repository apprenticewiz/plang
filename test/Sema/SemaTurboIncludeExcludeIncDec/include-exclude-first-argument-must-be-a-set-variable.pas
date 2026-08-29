(*
Include(s, x) / Exclude(s, x) mutate a set variable in place, so their
first argument must be one -- checkCallStmt's Include/Exclude arm refuses
a non-set argument (err_set_argument, the same diagnostic Card's own
argument-shape check already uses) before ever asking whether it is an
lvalue.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'include' requires a set argument, got 'integer'
*)

program p;
var
  i: Integer;
begin
  i := 5;
  Include(i, 5);
end.
