(*
Issue #192: packedAccessAlign (CGFieldAccess.cpp) answered `align 1` only
for a direct r.field FieldExpr -- see the sibling test
a-packed-field-does-not-claim-an-alignment-it-cannot-keep.pas for that
case.  `g.a[i]` reaches the same packed field through an IndexExpr
wrapped around the FieldExpr instead, which packedAccessAlign never
recursed into, so indexing a packed record's array field kept the
element type's default ABI alignment on a load/store that is really at
byte offset 1 in a packed struct -- the same class of promise nothing
made true, just reached by subscripting instead of a bare field access.

Checked as an IR shape rather than executed like the sibling test: unlike
the wider i256 set store that test's shape produces, a scalar i64
load/store through a wrongly-claimed align 8 is not guaranteed to pick
an alignment-sensitive instruction on every backend, so execution is not
reliable evidence either way -- the alignment attribute codegen emits is
the actual thing under test.  %plang_ir (not %plang) so a CI job that
reruns the suite at a higher default -O level cannot fold the indexed
load/store away first, the way constant propagation already does at -O1
for this same shape.

The struct spelling checked below can only ever appear as real LLVM
output, never as valid Pascal (ISO §6.1.8 lets a `}` end a `(* *)`
comment exactly as `*)` does, so a literal `<{ ... }>` cannot sit inside
one), so it lives outside the compiled chunk -- see split-file below,
same as the sibling struct-shape test
a-packed-record-is-built-as-a-packed-struct.pas.

RUN: split-file %s %t.dir
RUN: %plang_ir -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK: store i64 9, ptr getelementptr (<{ i8, [3 x i64] }>, ptr @pasg_g, i32 0, i32 1), align 1
CHECK: load i64, ptr getelementptr (<{ i8, [3 x i64] }>, ptr @pasg_g, i32 0, i32 1), align 1
*)

//--- test.pas
program p(output);
type pr = packed record c: char; a: array[1..3] of integer end;
var g: pr;
begin
  g.c := 'A'; g.a[1] := 9;
  writeln(g.c, ' ', g.a[1]:1)
end.
