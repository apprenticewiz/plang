(*
`set of char` lowers to i256, ABI-aligned to 16; codegen emits set
accesses with the alignment of the TYPE. The blob a variant part reserves
used to cap its cell at i64 (8-aligned), so a set stored inside one sat at
an 8-aligned offset while the compiler promised LLVM `align 16` on every
load and store of it -- an aligned vector access is within its rights to
fault on that. The cap was written into codegen's own blob-type
construction, into Sema's own size computation, and a third time into
Sema's byteSizeOf; the runtime layout walk was the only one of the three
that had it right, and nothing compared them until this became a
regression test. The struct spelling checked below can only ever appear
as real LLVM output, never as valid Pascal, so it lives outside the
compiled chunk -- see split-file below.

RUN: split-file %s %t.dir
RUN: %plang -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS %s
*)

(*
CHECK: { i1, [2 x i128] }
CHECK-NOT: { i1, [4 x i64] }
RUNS: yes
*)

//--- test.pas
program p(output);
type r = record
  case a: boolean of
    true:  (i: integer);
    false: (s: set of char)
end;
var v: r;
begin v.s := ['x']; if 'x' in v.s then writeln('yes') end.
