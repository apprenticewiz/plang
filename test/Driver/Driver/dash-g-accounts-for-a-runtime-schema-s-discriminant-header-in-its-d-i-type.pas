(*
Issue #122.  A schema reached through a pointer whose domain is UNDISCRIMINATED
(`^Rec2` below, discriminants supplied only by new()'s own arguments) has a
run-time extent -- CGDebugInfo::debugTypeOfSemaType's own Schema/SchemaInstance
case calls this ExtentVaries -- and SchemaLayoutEngine::schemaHeaderBytes /
SchemaAccess::emitNewSchema/schemaRefOf store the live discriminant(s) as a
leading header word in front of the body at run time (one 8-byte word per
discriminant here, so 8 bytes for Rec2's one).  The DIType this built used to
recurse straight into the probe-resolved body with NO header accounted for at
all, so EVERY field's DWARF offset -- not just an approximated extent for a
varying field -- was shifted by the header's width: gdb read 'n' where the
header actually put nothing, 'k' where 'n' really sits, and so on down the
whole record.  See buildSchemaDIType's own comment for the fix and its one
documented remaining gap (a FIXED field that comes AFTER a varying-extent
field still reads its offset off the probe's approximated extent, not the
real one -- only a field at or before the varying one is now exact).  This
test's 'k' precedes the varying 'a', so its own offset is now exact end to
end -- verified with a real gdb session (not just this IR-text check; ISO
10206 debug-info work needs both, per this project's standing rule) showing
'q^.k' and 'q^.a[1]' both correct instead of the completely-wrong values
every field showed before this fix.
*)

(*
RUN: %plang_ir -g -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
type Rec2(n: integer) = record k: integer; a: array[1..n] of integer end;
type Rec2Ptr = ^Rec2;
var q: Rec2Ptr;
begin
  new(q, 3);
  q^.k := 777;
  q^.a[1] := 11;
  writeln(q^.k);
  writeln(q^.a[1])
end.

(*
Note for whoever edits the CHECK block below: FileCheck's regex delimiters
are deliberately NOT used anywhere in this file.  ISO Pascal comments
accept EITHER terminator for EITHER opener (see Scanner::skipComment's own
comment), so a stray close-brace inside a paren-star-opened comment --
exactly what a FileCheck regex needs -- ends the comment right there, and
everything after it is parsed as code.  Every CHECK line below is
therefore a plain literal substring, not a regex.
*)

(*
CHECK-DAG: !DIGlobalVariable(name: "q", scope:
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Rec2",
CHECK-DAG: size: 192, align: 64, elements:
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "n",
The body's own member has no name (it is transparent to gdb, an ordinary
anonymous-struct member) and starts at offset 64 -- past the one 8-byte
discriminant word, not at offset 0.
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, scope:
CHECK-DAG: size: 128, align: 64, offset: 64)
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "record (k, a)",
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "k",
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a",
CHECK-DAG: align: 64, offset: 64)
*)
