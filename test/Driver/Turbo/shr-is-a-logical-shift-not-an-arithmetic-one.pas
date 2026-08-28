(*
Turbo's 'shr' is a LOGICAL right shift even on a signed Integer operand --
CGBinaryOps.cpp's Shl/Shr case uses LLVM's CreateLShr, never CreateAShr, so
a negative operand's vacated top bits fill with zero, NOT the sign bit the
way an arithmetic shift (or `div` by a power of two) would.  This is a
deliberate difference from `div`: `(-5) div 2` rounds toward zero, -2; a
LOGICAL `(-5) shr 1` instead treats -5's 16-bit two's-complement bit
pattern (0xFFFB) as an unsigned quantity and shifts THAT right by one,
landing on 32765 (0x7FFD) -- as far from -2 as it is possible to be, which
is exactly the point of this test: an accidental CreateAShr would produce
-3 here (arithmetic shift of -5 right by one, rounding toward negative
infinity), not 32765, so this distinguishes a correct logical shift from
both the arithmetic-shift and the div-style answers, not just from "did it
crash."
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:32765
CHECK-NEXT:-2
*)

program shr_is_logical;
begin
  writeln((-5) shr 1);   { logical: 0xFFFB shr 1 = 0x7FFD = 32765 }
  writeln((-5) div 2)    { arithmetic-rounding div, for contrast: -2 }
end.
