(*
Tier 2 capstone: existing coverage
(test/CodeGen/Turbo/unsigned-sized-integer-types-compare-unsigned.pas) only
ever compares an unsigned rung against ANOTHER VALUE OF ITS OWN SAME TYPE
(Byte vs Byte, Word vs Word, ...).  This exercises the case that test does
not: a SIGNED rung compared directly against an UNSIGNED one, at every
width the ladder offers, plus one cross-width pair, in one place.

issue #629 UPDATE: this test's CHECK block used to pin the pre-fix BUG this
same repro exposed -- for two operands that already shared one LLVM width
(Integer/Word both i16, LongInt/Cardinal both i32, Int64/QWord both i64,
ShortInt/Byte both i8), CGBinaryOps::emitBinary used to skip its own
widening step entirely (it only ran when the two operands' LLVM types
already DIFFERED) and compare the SIGNED side's own bit pattern read as
UNSIGNED whenever either operand's Sema type was unsigned, which is what
made a signed -1 (every bit set) compare as the largest possible value of
that width, not as "negative, so less": '-1 < 1' used to read FALSE and
'-1 > 1' used to read TRUE for all four same-width pairs below.  Fixed by
promoting a mixed-sign pair to a WIDER SIGNED type first (CGBinaryOps.cpp's
own comment on the fix has the full derivation, confirmed against real
`fpc -Mtp`) -- '-1 < 1' now correctly reads TRUE for every rung including
the top one (Int64/QWord): even though nothing wider than 64 bits exists to
promote into there, a TIED rank (both operands already 64 bits) still
resolves the comparison as SIGNED rather than falling back to unsigned,
confirmed empirically against `fpc -Mtp` (a narrower-signed-vs-QWord mix,
which IS not tied, does fall back to unsigned -- see CGBinaryOps.cpp's
comment for that corner).

'LongInt < Byte' adds one CROSS-width pair on top (a 32-bit signed value
against an 8-bit UNSIGNED one, needing a real extension step) -- Byte is
genuinely unsigned, so it zero-extends correctly regardless of width; this
pair was ALREADY correct before the #629 fix (the differing-width case
already took the wider, signed operand's own type), so it is included here
as a non-regression check alongside the five pairs the fix changed.

NOTE: this test deliberately does NOT include a cross-width pair where a
SIGNED narrower operand (e.g. ShortInt) is the one that needs extending
against a WIDER unsigned or differently-signed type.  Doing so would
surface a genuine, SEPARATE, still-unfixed bug unrelated to #629: some
CGExprCore::coerceToType/toI64 call sites omit the operand's actual Sema
Type::IsSigned and fall back to deciding zero-extend-vs-sign-extend from
the LLVM WIDTH alone (only i1/i8 zero-extend; everything else
sign-extends) -- CGBinaryOps::emitBinary's own operand-widening block is
NOT one of those call sites (it always passes an explicit operandIsSigned
answer, never omits it), so #629's fix does not touch this separate issue
either way; it remains out of scope here.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE TRUE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
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
