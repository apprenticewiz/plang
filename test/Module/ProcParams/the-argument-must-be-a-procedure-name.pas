(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure run(procedure a(x: integer));
begin a(1) end;
begin run(42) end.

(*
CHECK: must be the name of a procedure
*)
