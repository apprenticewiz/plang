(*
issue #630: Sema::commonIntType used to resolve a mixed-sign pair of EQUAL
width to an UNSIGNED type at that SAME width ("Word + ShortInt comes back
unsigned"), and CGBinaryOps::emitBinary's operand-widening block agreed
(only widening when the two operands' LLVM types had different bit widths
in the first place) -- so `Integer(-1) * Word(65535)` computed and wrapped
at 16 bits instead of promoting to a width wide enough to hold the true
product, printing 1 where real `fpc -Mtp` (and real Turbo Pascal 7, which
promotes mixed Integer/Word to LongInt) prints -65535.

Fixed by promoting a same-width (or unsigned-operand-at-least-as-wide)
mixed-sign pair to a WIDER SIGNED type before computing -- double the
unsigned operand's own width, capped at 64 bits where nothing wider exists
-- matching CGBinaryOps.cpp's identical fix for issue #629's comparisons
(the two are governed by the same table, kept in lockstep: Sema::
commonIntType's own comment has the full derivation).  This truth table
exercises all three narrower rungs the ladder has (ShortInt/Byte at 8 bits
promote to 16-bit signed, Integer/Word at 16 promote to 32-bit signed,
LongInt/Cardinal at 32 promote to 64-bit signed), both a `*` (arithmetic)
and an `or` (bitwise) operator, all confirmed against real `fpc -Mtp`.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8: -200 -1
CHECK-NEXT:16: -65535 -1
CHECK-NEXT:32: -4294967295 -1
*)

program p;
var
  si: ShortInt; by: Byte; s16: Integer;
  i: Integer; w: Word; li32: LongInt;
  li: LongInt; c: Cardinal; li64: Int64;
begin
  si := -1; by := 200;
  s16 := si * by;
  writeln('8: ', s16, ' ', si or by);

  i := -1; w := 65535;
  li32 := i * w;
  writeln('16: ', li32, ' ', i or w);

  li := -1; c := 4294967295;
  li64 := li * c;
  writeln('32: ', li64, ' ', li or c)
end.
