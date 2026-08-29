(*
A third symptom of the same root cause as the shl/shr and mixed-width-
arithmetic bugs fixed alongside this test (see the sibling
shl-shr-compute-at-the-shifted-operands-own-width-not-the-dialect-default.pas
and mixed-width-and-signedness-arithmetic-widens-by-each-operands-own-
signedness.pas): CGBinaryOps.cpp's Div/Mod case builds its div-by-zero and
div-overflow guards (RangeCheckGuards.h's emitDivZeroCheck/
emitDivOverflowCheck) at e.ResolvedType->Width -- the div/mod EXPRESSION's
own Sema-resolved width.  Sema::checkBinary's Div/Mod arm used to return
the dialect's default TyInt (16 bits under Turbo) UNCONDITIONALLY,
regardless of the operands' actual width, so the guards for a division
between two 64-bit Int64 operands ran on the divisor/dividend TRUNCATED
down to their low 16 bits.

That is not merely "checks the wrong bound" -- it is a false RUNTIME TRAP
on a perfectly valid division: `131072 div 65536` (2, no remainder) has a
divisor whose low 16 bits are all zero (65536 = 0x10000), so the
truncated-to-16-bit div-by-zero guard saw 0 and raised a spurious "Runtime
error 200" (division by zero) on a program that never divided by zero at
all.  Confirmed against the pre-fix compiler: it aborts this exact program
with exit code 200 instead of printing 2 and 0.

Now that Sema::checkBinary's Div/Mod arm answers Sema::commonIntType(Lt,
Rt) -- the wider of the two operands' own Width -- divBitsTy/modBitsTy in
CGBinaryOps.cpp are the divisor's REAL 64 bits here, so the guard sees
65536 (not 0) and the division proceeds normally.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:0
*)

program div_mod_guard_width;
var
  n, d: Int64;
begin
  n := 131072;  { 2 * 65536 }
  d := 65536;   { low 16 bits all zero -- used to falsely read as 0 }
  writeln(n div d);   { 2 }
  writeln(n mod d)     { 0 }
end.
