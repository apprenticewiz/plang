(*
Issue #776: same widening as
a-constant-out-of-range-byte-assignment-is-reported.pas, exercised against
Word (TypeKind::Integer, Width == 16, unsigned; ordinalRange answers
0..65535) instead of Byte.
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 70000 is outside the range 0..65535
*)

program p;
var w: Word;
begin
  w := 70000
end.
