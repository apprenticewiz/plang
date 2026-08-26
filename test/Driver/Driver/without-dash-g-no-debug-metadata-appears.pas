(*
RUN: %plang_ir -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
begin writeln('hi') end.

(*
CHECK-NOT: !llvm.dbg.cu
CHECK-NOT: DICompileUnit
*)
