(*
Issue #776: warnIfConstantOutOfRange (SemaStmt.cpp) only fired for an
explicitly-declared subrange type (TypeKind::Subrange), not for a named
built-in ranged integer type like Byte (TypeKind::Integer, Width == 8,
unsigned) -- `b := 300` compiled silently under -std=turbo even though the
same out-of-range constant assigned to a hand-written `1..255` subrange
already warned. The checker now consults Type.h's ordinalRange, which
answers a bounded range for Byte/Word/ShortInt/... the same way it already
does for an explicit subrange, so this now warns too.
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 300 is outside the range 0..255
*)

program p;
var b: Byte;
begin
  b := 300
end.
