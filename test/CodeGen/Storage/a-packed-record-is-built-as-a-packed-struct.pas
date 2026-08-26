(*
`<{ }>` is LLVM's spelling of a struct with no padding in it -- ISO
Sec6.4.3.1 leaves what `packed` does to the implementation, and plang
packs a packed record with no inter-field or tail padding at all, in
every dialect. All three fields are round-tripped, not just compiled, so
a correct total size with a field at the wrong offset would still be
caught here (see also
test-lit/CodeGen/Storage/a-packed-field-does-not-claim-an-alignment-it-cannot-keep.pas
for the same claim against a 16-aligned member). The struct spelling
checked below can only ever appear as real LLVM output, never as valid
Pascal, so it lives outside the compiled chunk -- see split-file below.

RUN: split-file %s %t.dir
RUN: %plang -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: <{ i8, i64, i8 }>
RUNS:x 42y
*)

//--- test.pas
program p(output);
type k = packed record a: char; b: integer; c: char end;
var v: k;
begin v.a := 'x'; v.b := 42; v.c := 'y'; writeln(v.a, v.b:3, v.c) end.
