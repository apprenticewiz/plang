(*
issue #577 (reopened): the STORED half of the truncation model
integer-literal-arithmetic-wraps-the-same-as-an-equal-valued-variable-
issue-577.pas's own comment describes -- real `fpc -Mtp` (confirmed
against a local build) narrows an arithmetic result to the destination
variable's own declared width ONLY when the result is actually stored
into one, not on every binary operation.  This test is the STORE side of
that pair: every row below stores `var + var` and `var + <an equal-
valued literal>` into a like-width variable and prints the STORED
variable, not the raw expression -- so, unlike the sibling (unstored)
test, wraparound IS expected here, and var+var must still agree with
var+literal for the identical reason issue #577 was originally filed:
plang's own two operand shapes must never disagree with each other.

Covers Byte (8-bit unsigned) -- the width the original #577 fix (PR #756)
left unfixed, per the reopening comment -- alongside ShortInt (8-bit
signed), Integer (16-bit signed), and LongInt (32-bit signed) for the
same var-vs-literal consistency, all confirmed to match real `fpc -Mtp`.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:byte varvar: 144
CHECK-NEXT:byte varlit: 144
CHECK-NEXT:shortint varvar: -56
CHECK-NEXT:shortint varlit: -56
CHECK-NEXT:integer varvar: -5536
CHECK-NEXT:integer varlit: -5536
CHECK-NEXT:longint varvar: -294967296
CHECK-NEXT:longint varlit: -294967296
*)

program p;
var
  by1, by2, by3: Byte;
  si1, si2, si3: ShortInt;
  i1, i2, i3: Integer;
  li1, li2, li3: LongInt;
begin
  by1 := 200; by2 := 200;
  by3 := by1 + by2;       writeln('byte varvar: ', by3);
  by3 := by1 + 200;       writeln('byte varlit: ', by3);

  si1 := 100; si2 := 100;
  si3 := si1 + si2;       writeln('shortint varvar: ', si3);
  si3 := si1 + 100;       writeln('shortint varlit: ', si3);

  i1 := 30000; i2 := 30000;
  i3 := i1 + i2;           writeln('integer varvar: ', i3);
  i3 := i1 + 30000;        writeln('integer varlit: ', i3);

  li1 := 2000000000; li2 := 2000000000;
  li3 := li1 + li2;         writeln('longint varvar: ', li3);
  li3 := li1 + 2000000000;  writeln('longint varlit: ', li3);
end.
