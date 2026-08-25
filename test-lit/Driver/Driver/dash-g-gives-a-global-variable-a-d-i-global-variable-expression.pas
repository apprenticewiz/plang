(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
var x: integer;
begin x := 1; writeln(x) end.

(*
CHECK-DAG: DIGlobalVariableExpression
CHECK-DAG: !DIGlobalVariable(name: "x"
*)
