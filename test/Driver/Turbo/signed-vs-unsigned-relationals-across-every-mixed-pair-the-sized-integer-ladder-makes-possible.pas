(*
Tier 2 capstone: existing coverage
(test/CodeGen/Turbo/unsigned-sized-integer-types-compare-unsigned.pas) only
ever compares an unsigned rung against ANOTHER VALUE OF ITS OWN SAME TYPE
(Byte vs Byte, Word vs Word, ...).  This exercises the case that test does
not: a SIGNED rung compared directly against an UNSIGNED one, at every
width the ladder offers, plus one cross-width pair, in one place.

For two operands that already share one LLVM width (Integer/Word both i16,
LongInt/Cardinal both i32, Int64/QWord both i64, ShortInt/Byte both i8),
CGBinaryOps::emitBinary needs no widening step at all -- it compares the
SIGNED side's own bit pattern read as UNSIGNED whenever either operand's
Sema type is unsigned (OrdinalIsUnsigned), which is what makes a signed -1
(every bit set) compare as the largest possible value of that width, not
as "negative, so less": '-1 < 1' is FALSE and '-1 > 1' is TRUE for all four
same-width pairs below.  'LongInt < Byte' adds one CROSS-width pair on top
(a 32-bit signed value against an 8-bit UNSIGNED one, needing a real
extension step) -- Byte is genuinely unsigned, so it zero-extends
correctly regardless of width.

NOTE: this test deliberately does NOT include a cross-width pair where a
SIGNED narrower operand (e.g. ShortInt) is the one that needs extending
against a WIDER unsigned or differently-signed type.  Doing so surfaced a
genuine, separate bug this session's testing found and is NOT fixing
(flagged in this PR's own description for review): CGExprCore::
coerceToType/toI64 decide zero-extend-vs-sign-extend from the LLVM WIDTH
alone (only i1/i8 zero-extend; everything else sign-extends), not the
operand's actual Sema Type::IsSigned -- so a signed ShortInt (i8) widened
against a wider type is incorrectly ZERO-extended (losing its sign)
instead of sign-extended, and a Word/Cardinal/LongWord (unsigned, i16/i32)
widened against a WIDER type is incorrectly SIGN-extended instead of
zero-extended once its own top bit is set.  For '-1' specifically the
wrong-extension bug happens not to flip the final TRUE/FALSE relational
answer here (both the correct and the buggy magnitude are simply "large,"
so either reads as greater than 1) -- exactly the kind of coincidence that
would make a regression test pass while actually pinning broken behavior,
which is why this test stops at the one cross-width pair that is
genuinely unaffected rather than every combination the ladder permits.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE TRUE
CHECK-NEXT:FALSE TRUE
CHECK-NEXT:FALSE TRUE
CHECK-NEXT:FALSE TRUE
CHECK-NEXT:FALSE TRUE
CHECK-NEXT:FALSE TRUE
*)

program mixed_relationals;
var
  i: Integer;  w:  Word;
  li: LongInt; ca: Cardinal;
  i6: Int64;   qw: QWord;
  sb: ShortInt; by: Byte;
begin
  { same width, ordinary in-range values: 100 < 60000 is unsurprising }
  i := 100; w := 60000;
  writeln(i < w, ' ', w > i);

  { same width, -1 vs 1: the mixed-signedness case that actually matters }
  i := -1; w := 1;
  writeln(i < w, ' ', i > w);

  sb := -1; by := 1;
  writeln(sb < by, ' ', sb > by);

  li := -1; ca := 1;
  writeln(li < ca, ' ', li > ca);

  i6 := -1; qw := 1;
  writeln(i6 < qw, ' ', i6 > qw);

  { one cross-width pair: LongInt (32-bit signed) against Byte (8-bit,
    genuinely unsigned, so it zero-extends correctly regardless of width) }
  li := -1; by := 1;
  writeln(li < by, ' ', li > by);
end.
