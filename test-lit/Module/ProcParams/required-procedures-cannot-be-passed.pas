(*
RUN: not %plang_ir -emit-llvm %s -o %t.ll 2> %t.err
RUN: FileCheck %s < %t.err
*)

program p;
procedure run(procedure a(x: integer));
begin a(1) end;
begin run(writeln) end.

(*
CHECK: required procedure
*)
