(*
Mark, Release, Keep, Intr, MsDos, GetIntVec and SetIntVec are ordinarily
called as procedure STATEMENTS in real Turbo Pascal code -- `Intr($21,
Regs);`, `Mark(P);` -- never used as expression values.  A rejection list
wired only into the expression-context path (checkIdent/checkCallExpr, both
in SemaExpr.cpp) would never see these: a bare `Intr($21, Regs);` statement
goes through checkCallStmt (SemaStmt.cpp) instead, which normally raises
err_undefined_procedure for a name nothing declares.  This is the important
distinction the task called out explicitly -- confirms both hook points
actually fire, not just the expression one.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'Mark' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Release' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Keep' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Intr' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'MsDos' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'GetIntVec' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'SetIntVec' is a real-mode DOS facility and has no meaning under -std=turbo on this target
*)

program p;
var p1, p2: integer;
begin
  Mark(p1);
  Release(p1);
  Keep(0);
  Intr($21, p1);
  MsDos(p1);
  GetIntVec(1, p1);
  SetIntVec(1, p2)
end.
