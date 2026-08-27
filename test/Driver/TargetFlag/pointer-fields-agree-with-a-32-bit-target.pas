(*
Issue #243's follow-up.  The original #243 fix threads --target's triple
through to CodeGen, so the module's data layout is genuinely non-host for a
target whose pointers are not 8 bytes wide -- but Sema::byteSizeOf/
byteAlignOf's Pointer/Nil/String cases (SemaType.cpp) hardcoded 8 regardless
of --target, a disagreement that was unreachable, and so invisible, for as
long as --target could never actually make CodeGen's own answer differ from
the host's.  CGTypes::checkSizeAgreement (CGTypes.cpp) -- the oracle that
cross-checks Sema's opinion of a type's size against the LLVM type CodeGen
actually built for it -- correctly caught the disagreement once it became
reachable, and aborted compilation rather than silently emitting a record
whose pointer field was laid out 4 bytes wide while Sema's own byte-offset
arithmetic still assumed 8: reproduced directly, `-g
--target=arm-unknown-linux-gnueabi -c` on a record with a pointer field
crashed with "LLVM ERROR: plang codegen: type '^integer' is 8 bytes to
Sema and 4 bytes as it was laid out" before this fix.  The fix stamps the
target's real pointer width onto every Pointer/Nil/String Type at the one
place they are minted (TypeContext, mirroring how it already stamps an
Integer's dialect-dependent width), so byteSizeOf/byteAlignOf need no
target-awareness of their own.

CGDebugInfo.cpp's own DWARF construction had the identical bug one level
further out, not caught by any oracle: every createPointerType call (and
the closure-pair and schema-shadow struct layouts built alongside a couple
of them) hardcoded 64 (bits) for a pointer's declared DWARF size, which
nothing cross-checks against the module's real data layout -- so a
debugger would have kept silently being told every pointer is 8 bytes on
any target where it is not, with no internal error to reveal it.  Fixed by
asking the module's own (by now target-correct) data layout instead.

The compiled program lives in a split-file chunk rather than directly in
this file: see this file's sibling,
target-flag-sets-the-i-r-triple-and-data-layout.pas, for why -- FileCheck's
regex-capture punctuation collides with ISO 7185 6.1.8's comment-terminator
rule the same way there as here.

RUN: split-file %s %t.dir
RUN: %plang_ir -g --target=arm-unknown-linux-gnueabi -c %t.dir/test.pas -o %t.o
RUN: %plang_ir -g --target=arm-unknown-linux-gnueabi -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
32-bit ARM's pointers are 4 bytes (32 bits) -- never a plausible accident of
Sema quietly falling back to the HOST's own width, since every architecture
this suite's own CI builds and runs on (x86_64, aarch64, arm64 Darwin) is
64-bit.  "size: 32" below cannot pass by --target's width merely defaulting
to whatever machine happened to run the test.

CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: {{.*}}, size: 32)
CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "p", {{.*}}size: 32, align: 32, offset: 32)
CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "rec", {{.*}}size: 64, align: 32
*)

//--- test.pas
program p;
type
  pint = ^integer;
  rec = record
    c: char;
    p: pint;
  end;
var
  r: rec;
begin
  new(r.p);
  r.p^ := 1;
  dispose(r.p)
end.
