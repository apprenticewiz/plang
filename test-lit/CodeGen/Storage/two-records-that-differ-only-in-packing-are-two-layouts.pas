(*
The struct-type cache keys on the field types, so without packing in the
key these two would share one struct type -- and whichever was built
second would silently take the first's offsets.  Same field list, one
plain and one packed, compiled together: both layouts must appear as
distinct LLVM types, and both must round-trip correctly. The struct
spellings checked below can only ever appear as real LLVM output, never
as valid Pascal, so they live outside the compiled chunk -- see
split-file below.

RUN: split-file %s %t.dir
RUN: %plang -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS %s
*)

(*
CHECK-DAG: <{ i8, i64 }>
CHECK-DAG: pasg_vu = global { i8, i64 }
RUNS: 3
*)

//--- test.pas
program p(output);
type u =        record a: char; b: integer end;
     k = packed record a: char; b: integer end;
var vu: u; vk: k;
begin vu.b := 1; vk.b := 2; writeln(vu.b + vk.b) end.
