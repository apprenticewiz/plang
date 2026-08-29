(*
Tier 2 capstone: no single-width test exercises the FULL sized-integer
ladder's wraparound behavior in one place.  Real Turbo Pascal ships with
its OverflowChecks switch (Q) off by default -- plang's own OverflowChecks
switch is "recorded only" (docs/turbo.md), never acted on regardless of its
directive-controlled state --
so a plain '+'/'-' at every one of the ladder's eight distinct widths wraps
silently, exactly like real hardware two's-complement arithmetic, both
signed and unsigned.  This walks max+1 and min-1 (0-1 for the unsigned
rungs) at ShortInt(8)/Byte(8)/Integer(16)/Word(16)/LongInt(32)/
Cardinal(32)/Int64(64)/QWord(64), plus a sanity check that SmallInt and
LongWord -- literally the SAME interned Type object as Integer and
Cardinal respectively (TypeContext::getInt keys on the pair of Width and
Signed alone, see docs/turbo.md's sized-integer ladder section) -- wrap identically to
the names they alias, not merely similarly.

QWord's own maximum has no literal spelling (int64_t literals cap out at
Int64's own maximum) -- QWord(-1) is the documented workaround (see
docs/turbo.md's "no QWord literal syntax" deviation entry), the same idiom
test/CodeGen/Turbo/qword-write-and-read-round-trip-the-full-unsigned-range.pas
already uses.  Int64's own minimum is written as the standard
'-9223372036854775807 - 1' idiom for the identical reason: a bare
-9223372036854775808 literal would first parse the positive magnitude
9223372036854775808, one past Int64's own max, before any unary minus is
applied.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-128
CHECK-NEXT:127
CHECK-NEXT:0
CHECK-NEXT:255
CHECK-NEXT:-32768
CHECK-NEXT:32767
CHECK-NEXT:0
CHECK-NEXT:65535
CHECK-NEXT:-2147483648
CHECK-NEXT:2147483647
CHECK-NEXT:0
CHECK-NEXT:4294967295
CHECK-NEXT:-9223372036854775808
CHECK-NEXT:9223372036854775807
CHECK-NEXT:0
CHECK-NEXT:18446744073709551615
CHECK-NEXT:-32768
CHECK-NEXT:0
*)

program wraparound_ladder;
var
  si: ShortInt;
  by: Byte;
  ii: Integer;
  wo: Word;
  li: LongInt;
  ca: Cardinal;
  i6: Int64;
  qw: QWord;
  smallCheck: SmallInt;
  longwCheck: LongWord;
begin
  si := 127;    si := si + 1;  writeln(si);
  si := -128;   si := si - 1;  writeln(si);
  by := 255;    by := by + 1;  writeln(by);
  by := 0;      by := by - 1;  writeln(by);
  ii := 32767;  ii := ii + 1;  writeln(ii);
  ii := -32768; ii := ii - 1;  writeln(ii);
  wo := 65535;  wo := wo + 1;  writeln(wo);
  wo := 0;      wo := wo - 1;  writeln(wo);
  li := 2147483647;    li := li + 1;  writeln(li);
  li := -2147483648;   li := li - 1;  writeln(li);
  ca := 4294967295;    ca := ca + 1;  writeln(ca);
  ca := 0;             ca := ca - 1;  writeln(ca);
  i6 := 9223372036854775807;  i6 := i6 + 1;  writeln(i6);
  i6 := -9223372036854775807 - 1;  i6 := i6 - 1;  writeln(i6);
  qw := QWord(-1);  qw := qw + 1;  writeln(qw);
  qw := 0;          qw := qw - 1;  writeln(qw);
  smallCheck := 32767;         smallCheck := smallCheck + 1; writeln(smallCheck);
  longwCheck := 4294967295;    longwCheck := longwCheck + 1; writeln(longwCheck);
end.
