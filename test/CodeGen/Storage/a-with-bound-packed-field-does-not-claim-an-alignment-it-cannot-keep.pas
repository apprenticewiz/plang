(*
Issue #192: packedAccessAlign (CGFieldAccess.cpp) answered `align 1` only
for a direct r.field FieldExpr -- see the sibling test
a-packed-field-does-not-claim-an-alignment-it-cannot-keep.pas for that
case.  `with g do` binds a packed record's fields as ordinary names
(IdentExpr) instead, which packedAccessAlign never looked inside, so the
exact promise that test already fixed for `g.cs := [...]` came back for
`with g do cs := [...]` and for reading a with-bound field back out:
IRBuilder kept i256's/i64's default ABI alignment on a load or store that
is really at byte offset 1 in a packed struct, which SIGSEGVs from -O1
once the backend picks an alignment-sensitive instruction for it (the
same crash the sibling test guards, just reached through `with` rather
than a bare field access).

Checked as an IR shape rather than executed and compared across -O0..-O3
like the sibling test: whether a given optimization level goes on to pick
an instruction that actually demands the (wrongly) claimed alignment is a
backend instruction-selection heuristic, not something this test should
have to depend on either way.  %plang_ir (not %plang) so a CI job that
reruns the suite at a higher default -O level cannot fold the with-bound
load/store away before this ever gets to check its alignment.

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
CHECK: store i256 %set, ptr getelementptr (<{ i8, i256, i64 }>, ptr @pasg_g, i32 0, i32 1), align 1
CHECK: store i64 5, ptr getelementptr (<{ i8, i256, i64 }>, ptr @pasg_g, i32 0, i32 2), align 1
CHECK: load i256, ptr getelementptr (<{ i8, i256, i64 }>, ptr @pasg_g, i32 0, i32 1), align 1
CHECK: load i64, ptr getelementptr (<{ i8, i256, i64 }>, ptr @pasg_g, i32 0, i32 2), align 1
*)

//--- test.pas
program p(output);
type pr = packed record c: char; cs: set of char; k: integer end;
var g: pr; b: boolean; n: integer;
begin
  with g do begin c := 'A'; cs := ['a'..'z']; k := 5; b := ('m' in cs); n := k end;
  writeln(g.c, ' ', b, ' ', n:1)
end.
