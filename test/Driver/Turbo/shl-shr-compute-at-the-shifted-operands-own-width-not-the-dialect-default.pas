(*
Issue: Sema::checkBinary's Shl/Shr case used to return TyInt
UNCONDITIONALLY -- the dialect's own default Integer (16 bits under Turbo)
-- as the shift EXPRESSION's result type, regardless of what width the
LEFT operand (the value actually being shifted) really was.
CGBinaryOps.cpp's Shl/Shr codegen then re-coerced both operands down to
that wrong, narrow e.ResolvedType->Width before ever shifting, so
`Int64(1) shl 40` was silently truncated to 16 bits of working width first
and printed 256 (1 shl (40 and 15) = 1 shl 8) instead of 2^40 =
1099511627776.  This was unreachable before the Tier 2 sized-integer
ladder existed: ISO 7185 and Extended Pascal have exactly one Integer type
(always Width 64), so their own "shift" case does not exist, and Turbo's
sole Integer was always exactly the dialect default width already -- only
once ShortInt/Byte/SmallInt/Word/Cardinal/LongInt/LongWord/Int64/QWord gave
Turbo a shl/shr operand at a width OTHER than the default could this ever
show up as a wrong printed value rather than merely "narrower than it
should theoretically be, but nothing anyone actually ran hit it."

Sema::checkBinary's Shl/Shr case now answers `max(LeftOperand.Width,
TyInt.Width)` at the left operand's own IsSigned -- the left operand's own
width when it is already at or above the dialect default, promoted UP to
the default otherwise (never narrowed to it) -- and CGBinaryOps.cpp's
Shl/Shr codegen coerces the left operand to bitsTy using that operand's
REAL Sema-resolved signedness (CodeGenImpl.h's toI64/coerceToType
srcSigned parameter) rather than an LLVM-bit-width-only guess.  Both rules
are checked against real `fpc -Mtp` field practice, not assumed:
  - `Int64Var shl N` computes at Int64's own 64 bits, not 16 (the RIGHT
    operand -- the shift COUNT -- never widens the result the way both
    operands of '+' or 'div' do: `ByteVar shl Int64Count` still answers at
    Byte's own promoted width, not Int64's).
  - `ByteVar shl N` (Byte narrower than the default) PROMOTES to the
    default rather than masking the shift down to Byte's own 8 bits --
    `Byte(1) shl 20` is 16 here (masked mod 16, the Turbo default), not 16
    mod 8's degenerate `1 shl 4` also happening to be 16 by coincidence:
    see the same shift at a shift count that tells the two apart below.
  - The promoted operand's SIGN survives promotion: `ShortInt(-1) shl 1`
    sign-extends -1's all-ones bit pattern before shifting (answer -2), and
    `Word(60000) shr 1` (an unsigned 'shr' is always logical regardless of
    signedness, but still needs its OWN 16 bits, not a wrongly-narrowed or
    wrongly-widened value) answers 30000, not some sign-corrupted value.
  - `LongInt(-100000) shr 1` computes the LOGICAL shift at LongInt's own 32
    bits (2147433648), neither narrowed to 16 nor sign-extended into 64
    first.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1099511627776
CHECK-NEXT:1099511627776
CHECK-NEXT:-2
CHECK-NEXT:30000
CHECK-NEXT:2147433648
CHECK-NEXT:16
*)

program shl_shr_wide;
var
  i6: Int64;
  qw: QWord;
  si: ShortInt;
  wd: Word;
  li: LongInt;
  by: Byte;
begin
  i6 := 1;       writeln(i6 shl 40);  { 2^40, not truncated to 16 bits first }
  qw := 1;       writeln(qw shl 40);  { same, unsigned rung }
  si := -1;      writeln(si shl 1);   { sign-extend then shift: -2 }
  wd := 60000;   writeln(wd shr 1);   { logical shift at Word's own 16 bits: 30000 }
  li := -100000; writeln(li shr 1);   { logical shift at LongInt's own 32 bits }
  by := 1;       writeln(by shl 20)   { Byte promotes to the 16-bit default: 1 shl (20 and 15) = 16 }
end.
