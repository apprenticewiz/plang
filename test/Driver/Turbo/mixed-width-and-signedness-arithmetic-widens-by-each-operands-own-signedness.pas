(*
Two related pre-existing bugs the Tier 2 sized-integer ladder finally made
observable (both unreachable under ISO 7185/Extended Pascal, whose one
Integer type is always Width 64/IsSigned true, and unreachable under Turbo
before the ladder gave it Integer-kind operands at more than one width):

1. Sema::checkBinary's Div/Mod/Xor/bitwise-And/Or arms, and numericResult
   (the shared '+'/'-'/'*' case), all used to return TyInt UNCONDITIONALLY
   -- the dialect's own default Integer -- as their result type, regardless
   of what width or signedness the operands actually were.  They now answer
   Sema::commonIntType(Lt, Rt): the WIDER of the two operands' own Width, at
   that wider operand's own IsSigned (ties -- equal width, different sign --
   go unsigned), all confirmed against real `fpc -Mtp` field practice.

2. CodeGenExprs.cpp's coerceToType/toI64 used to pick sign- vs.
   zero-extension from the LLVM VALUE's own bit width alone ("i8 or i1,
   zero-extend; anything else, sign-extend"), which only ever happened to be
   correct while Char/Boolean (unsigned) and Turbo's own plain signed
   Integer were the only narrower-than-64-bit ordinals in existence.  The
   ladder's unsigned rungs wider than i8 (Word/Cardinal/LongWord/QWord) and
   its signed 8-bit rung (ShortInt) broke that guess: `QWord(5000000000) +
   Cardinal(3000000000)` sign-extended the Cardinal operand (its top bit is
   set) into a huge negative i64 before the add, printing 3705032704 --
   5000000000 + 3000000000 - 2^32 -- instead of 8000000000.  Both functions
   now take an explicit srcSigned parameter, consulted at every call site in
   CGBinaryOps.cpp's binary-op widening/division/shift path, that carries
   the operand's actual Sema-resolved Type::IsSigned through instead.

Every value below is checked against real `fpc -Mtp` output where the
scenario translates directly (Word+ShortInt's unsigned tie-break, LongInt+
Word keeping LongInt's own sign, Byte widening into Int64 arithmetic,
Int64 div/xor against a narrower Byte), not merely "does not crash."
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8000000000
CHECK-NEXT:59999
CHECK-NEXT:4
CHECK-NEXT:3000000200
CHECK-NEXT:3333333333
CHECK-NEXT:10000000003
CHECK-NEXT:4294967295
CHECK-NEXT:255
*)

program mixed_width_sign;
var
  qw: QWord;
  cd: Cardinal;
  wd: Word;
  si: ShortInt;
  li: LongInt;
  by: Byte;
  i6: Int64;
begin
  { QWord + Cardinal: the Cardinal operand must zero-extend, not sign-extend }
  qw := 5000000000; cd := 3000000000;
  writeln(qw + cd);                    { 8000000000 }

  { Word + ShortInt, equal width (16 vs. promoted-8): unsigned wins the tie }
  wd := 60000; si := -1;
  writeln(wd + si);                    { 59999 }

  { LongInt + Word, different width: the WIDER operand's own (signed) sign wins }
  li := -1; wd := 5;
  writeln(li + wd);                    { 4 }

  { Cardinal + Byte: Byte zero-extends into the wider unsigned result }
  cd := 3000000000; by := 200;
  writeln(cd + by);                    { 3000000200 }

  { Int64 div/xor a narrower Byte: both compute at Int64's own 64 bits }
  i6 := 10000000000; by := 3;
  writeln(i6 div by);                  { 3333333333 }
  writeln(i6 xor by);                  { 10000000003 }

  { Turbo bitwise 'not' stays at the OPERAND's own width, not the default }
  cd := 0;  writeln(not cd);           { 4294967295, not a 16-bit-truncated answer }
  by := 0;  writeln(not by)            { 255 }
end.
