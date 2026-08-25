(*
One passes an address and the other a copy, so this is not a detail.

RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: not congruous
CHECK-DAG: procedure(var integer)
*)

program p;
var n: integer;
procedure run(procedure a(var x: integer));
begin a(n) end;
procedure act(x: integer); begin end;
begin run(act) end.
