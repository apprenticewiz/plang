(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type color = (red, green, blue);
var c: color;
begin c := green; writeln(ord(c)) end.

(*
CHECK-DAG: DW_TAG_enumeration_type, name: "color"
CHECK-DAG: !DIEnumerator(name: "red", value: 0)
CHECK-DAG: !DIEnumerator(name: "green", value: 1)
CHECK-DAG: !DIEnumerator(name: "blue", value: 2)
*)
