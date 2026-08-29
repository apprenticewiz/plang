(*
Codegen::Impl::toI64 and coerceToType widen an ordinal narrower than i64 to
compute or compare it -- an unconditional zero-extend was correct as long as
the only ordinals narrower than i64 were Char (i8) and Boolean (i1), both
genuinely non-negative.  Turbo's Integer is signed and 16-bit, so a negative
value zero-extended before arithmetic silently became a huge positive one:
-5 (i16, bit pattern 0xFFFB) zero-extended to i64 is 65531, not -5.  Both
helpers now sign-extend anything narrower than i64 that isn't Char/Boolean.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-10
CHECK-NEXT:-4
CHECK-NEXT:TRUE
CHECK-NEXT:FALSE
*)

program p;
var i: Integer;
begin
  i := -5;
  writeln(i * 2);
  writeln(i + 1);
  writeln(i < 0);
  writeln(i > 0)
end.
