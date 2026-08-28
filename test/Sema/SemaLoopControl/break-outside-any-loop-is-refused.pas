(*
TP-only Break (Builtins.def) is refused with no enclosing while/for/for-in/
repeat loop -- Sema.h's LoopDepth_, checked in checkCallStmt's own Break/
Continue arm.  At the program's own top level there is no loop at all.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'break' is not valid outside a while, for, for-in or repeat loop
*)

program p;
begin
  Break
end.
