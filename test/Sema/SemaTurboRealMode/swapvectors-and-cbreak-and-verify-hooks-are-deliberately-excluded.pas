(*
SwapVectors, GetCBreak, SetCBreak, GetVerify and SetVerify are deliberately
NOT on the real-mode-DOS rejection list, even though real Turbo Pascal
declares them in the same Dos unit as Intr/MsDos/GetIntVec/SetIntVec: real TP
code calls them unconditionally around Exec (SwapVectors swaps interrupt
vectors before/after shelling out; the CBreak/Verify pairs read and write
Ctrl-Break checking and disk-verify flags), so rejecting them outright would
break programs that would otherwise run fine.  A later task makes plang's Dos
unit accept-and-no-op them; this one must leave them exactly as they behave
today -- plain undefined identifiers/procedures, not the new specific
diagnostic.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: undefined procedure 'SwapVectors'
CHECK-DAG: undefined procedure 'GetCBreak'
CHECK-DAG: undefined procedure 'SetCBreak'
CHECK-DAG: undefined procedure 'GetVerify'
CHECK-DAG: undefined procedure 'SetVerify'
CHECK-NOT: real-mode DOS facility
*)

program p;
var b: integer;
begin
  SwapVectors;
  GetCBreak(b);
  SetCBreak(b);
  GetVerify(b);
  SetVerify(b)
end.
