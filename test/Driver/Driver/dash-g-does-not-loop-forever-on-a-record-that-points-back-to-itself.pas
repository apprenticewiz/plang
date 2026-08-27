(*
A DWARF pointer's own pointee DIType only needs to exist, not be
complete: this checks that a self-referential record (an ordinary
Pascal linked-list node, pointing back to its own type through a
pointer field) gets a real DIType and -g codegen terminates at all --
see CGDebugInfo::buildRecordDIType's own comment on the forward-
declaration cache entry this relies on.
*)

(*
RUN: %plang_ir -g -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p(output);
type
  pnode = ^node;
  node = record
    data: integer;
    next: pnode;
  end;
var n: node;
begin
  n.data := 5;
  n.next := nil;
  writeln(n.data)
end.

(*
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "node", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "data", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "next", scope:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType:
*)
