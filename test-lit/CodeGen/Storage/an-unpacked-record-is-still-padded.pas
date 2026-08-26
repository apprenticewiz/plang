(*
The other half of
test-lit/CodeGen/Storage/a-packed-record-is-built-as-a-packed-struct.pas:
packing one record must not pack every record.  Same field list, not
packed, must come out as an ordinarily-padded LLVM struct, not `<{ }>`.
The struct spellings checked below can only ever appear as real LLVM
output, never as valid Pascal, so they live outside the compiled chunk --
see split-file below.

RUN: split-file %s %t.dir
RUN: %plang -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK-NOT: <{ i8, i64, i8 }>
CHECK: { i8, i64, i8 }
RUNS:x 1y
*)

//--- test.pas
program p(output);
type u = record a: char; b: integer; c: char end;
var v: u;
begin v.a := 'x'; v.b := 1; v.c := 'y'; writeln(v.a, ' ', v.b, v.c) end.
