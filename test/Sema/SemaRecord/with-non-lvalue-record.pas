(*
ISO §6.8.3.10 / EP §6.8.3.10: a with-statement's record-variable is a
variable-access, so a function call's result -- which has no storage of
its own past the call -- may not be named there.  pushWithScope bound
whatever the expression evaluated to without checking it was an lvalue,
so `with mk() do x := 5` compiled and the assignment to `x` silently
landed in a temporary nothing could ever read back.
*)

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p(output);
type r = record x: integer end;
function mk: r;
begin mk.x := 1 end;
begin
  with mk() do x := 5
end.

(*
CHECK: with' requires a variable
*)
