(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
begin writeln('hi') end.

(*
CHECK-DAG: !llvm.dbg.cu
CHECK-DAG: DICompileUnit
CHECK-DAG: DIFile
CHECK-DAG: Debug Info Version
*)
