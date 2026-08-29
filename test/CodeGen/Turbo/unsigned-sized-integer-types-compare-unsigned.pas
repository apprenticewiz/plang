(*
Word/Cardinal/LongWord/QWord/Byte are unsigned rungs of the Turbo sized-
integer ladder (TypeContext::getInt, Type::IsSigned=false): a value whose
top bit is set is a large positive number, not a negative one, and the
relational operators must compare it that way.  Before ordinalIsUnsigned
(CodeGenImpl.h) was taught to read Type::IsSigned instead of dispatching on
Kind alone, every Integer-kind operand -- signed or not -- fell through to a
signed icmp, so 60000 (a huge positive Word) read as -5536 (its bit pattern
reinterpreted as signed i16) and compared LESS than 100.

See test/CodeGen/OrdinalCompare/ for the non-regression side of the same
fix: chars-above-the-signed-range-compare-high.pas,
booleans-order-false-before-true.pas and
enumerations-keep-declaration-order.pas all still need Boolean/Char/Enum
read as unsigned despite carrying Type::IsSigned=false themselves now
(Type::makeBoolean/makeChar, SemaType.cpp's EnumTypeNode arm), the exact
values this change could have silently flipped the wrong way.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
Turbo spells boolean results TRUE/FALSE (uppercase); see
plang_write*_i1's Upper flag and BuiltinIO.cpp's own comment on it.

CHECK:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
CHECK-NEXT:TRUE FALSE
*)

program p;
var
  by1, by2: Byte;
  wd1, wd2: Word;
  cd1, cd2: Cardinal;
  lw1, lw2: LongWord;
  qw1, qw2: QWord;
begin
  by1 := 200; by2 := 100;
  writeln(by1 > by2, ' ', by1 < by2);

  wd1 := 60000; wd2 := 100;
  writeln(wd1 > wd2, ' ', wd1 < wd2);

  cd1 := 4000000000; cd2 := 100;
  writeln(cd1 > cd2, ' ', cd1 < cd2);

  lw1 := 4000000000; lw2 := 100;
  writeln(lw1 > lw2, ' ', lw1 < lw2);

  { Integer-literal parsing is int64_t end-to-end (see the sized-integer-
    ladder test's own comment), so 2^63 -- the smallest QWord value whose
    sign bit is set -- cannot be written as a literal directly; built by
    shifting instead. }
  qw1 := 1;
  qw1 := qw1 shl 63;
  qw2 := 100;
  writeln(qw1 > qw2, ' ', qw1 < qw2)
end.
