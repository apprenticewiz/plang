(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type
  shape = (circleT, squareT);
  fig = record
    n: integer;
    case kind: shape of
      circleT: (radius: real);
      squareT: (side: real);
  end;
var f: fig;
begin
  f.n := 1;
  f.kind := circleT;
  f.radius := 2.5;
  writeln(f.n)
end.

(*
CHECK-DAG: !DIGlobalVariable(name: "f", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "fig", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "n", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "kind", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "$variant", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_union_type, name: "fig.$variant", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "radius", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "side", scope:
*)
