(*
issue #629: CGBinaryOps::emitBinary's operand-widening block only ran when
lv/rv's LLVM types had DIFFERENT bit widths -- so two Turbo sized-integer
operands of the SAME width but different signedness (Integer vs. Word, both
16 bits) skipped it entirely, and the comparison's own `uns` flag then went
purely on "is EITHER operand unsigned" (OrdinalIsUnsigned), which Word
always answers yes to.  `Integer(-1) = Word(65535)` therefore ran an
UNSIGNED 16-bit icmp, reading -1's bit pattern as 65535 and calling the two
equal, when real `fpc -Mtp` says they are not (a signed -1 and an unsigned
65535 are never the same value).

Fixed by promoting a same-width (or unsigned-operand-at-least-as-wide)
mixed-sign pair to a WIDER SIGNED type before comparing -- CGBinaryOps.cpp's
own top-of-file comment on the fix has the full derivation.  This truth
table exercises all three narrower rungs the ladder has (ShortInt/Byte at 8
bits, Integer/Word at 16, LongInt/Cardinal at 32 -- Int64/QWord at 64 has no
wider type to promote to and is deliberately not a regression target here,
see CGBinaryOps.cpp's comment for why fpc agrees that ONE rung should stay
unsigned), all three confirmed against real `fpc -Mtp`.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8: FALSE TRUE TRUE
CHECK-NEXT:16: FALSE TRUE TRUE
CHECK-NEXT:32: FALSE TRUE TRUE
*)

program p;
var
  si: ShortInt; by: Byte;
  i: Integer; w: Word;
  li: LongInt; c: Cardinal;
begin
  si := -1; by := 200;
  writeln('8: ', si = by, ' ', si < by, ' ', by > si);

  i := -1; w := 65535;
  writeln('16: ', i = w, ' ', i < w, ' ', w > i);

  li := -1; c := 4294967295;
  writeln('32: ', li = c, ' ', li < c, ' ', c > li)
end.
