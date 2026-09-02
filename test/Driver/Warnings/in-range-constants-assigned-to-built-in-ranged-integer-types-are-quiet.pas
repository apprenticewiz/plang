(*
Issue #776 companion: the fix that made warnIfConstantOutOfRange (SemaStmt.
cpp) consult Type.h's ordinalRange instead of hand-checking TypeKind::
Subrange must not turn into a false positive for a constant that genuinely
fits a built-in ranged integer type's own domain -- Byte's 0..255, Word's
0..65535, ShortInt's -128..127 -- in either a plain assignment or a
for-loop bound (both endpoints, at both edges of the range).
*)

(*
RUN: %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: is outside the range
*)

program p;
var
  b: Byte;
  w: Word;
  si: ShortInt;
begin
  b := 0;
  b := 255;
  for b := 1 to 255 do ;

  w := 0;
  w := 65535;
  for w := 1 to 65535 do ;

  si := -128;
  si := 127;
  for si := -128 to 127 do ;
end.
