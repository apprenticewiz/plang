(*
Issue #776: same widening as
a-constant-out-of-range-byte-assignment-is-reported.pas, exercised against
ShortInt (TypeKind::Integer, Width == 8, signed; ordinalRange answers
-128..127) instead of Byte -- confirms the fix also covers a signed
built-in ranged integer type, not just an unsigned one.
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 200 is outside the range -128..127
*)

program p;
var si: ShortInt;
begin
  si := 200
end.
