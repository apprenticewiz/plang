(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
var i: integer; r: real; b: boolean; c: char;
begin
  i := 1; r := 1.0; b := true; c := 'x';
  writeln(i)
end.

(*
CHECK-DAG: !DIBasicType(name: "integer", size: 64, encoding: DW_ATE_signed)
CHECK-DAG: !DIBasicType(name: "real", size: 64, encoding: DW_ATE_float)
CHECK-DAG: !DIBasicType(name: "boolean", size: 8, encoding: DW_ATE_boolean)
CHECK-DAG: !DIBasicType(name: "char", size: 8, encoding: DW_ATE_unsigned_char)
*)
