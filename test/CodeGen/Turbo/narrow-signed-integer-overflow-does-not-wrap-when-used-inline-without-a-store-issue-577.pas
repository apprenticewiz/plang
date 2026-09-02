(*
issue #577 (reopened): PR #756's own fix for the ORIGINAL #577 report
(narrowing a literal operand's promotion width to match a narrower
variable it was paired with) introduced a regression for exactly the
inverse shape -- a narrow SIGNED sized-integer variable (ShortInt/
Integer/LongInt) whose overflowing arithmetic result is consumed INLINE,
with no intervening store, e.g. a bare write argument:

    ii := 32767; Writeln(ii + 1);

plang (post-#756) printed -32768 there; real `fpc -Mtp` (confirmed
against a local build) prints 32768 -- fpc never wraps an unstored
expression at all (see this directory's
integer-literal-arithmetic-wraps-the-same-as-an-equal-valued-variable-
issue-577.pas for the fuller derivation and the matching STORED-case
sibling test). Exercises every signed rung narrower than 64 bits
(ShortInt, Integer, LongInt) with both a literal and an equal-valued
variable right operand, confirming the two agree (issue #577's own
original concern) as well as matching fpc's un-wrapped value.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:shortint lit: 128
CHECK-NEXT:shortint var: 128
CHECK-NEXT:integer lit: 32768
CHECK-NEXT:integer var: 32768
CHECK-NEXT:longint lit: 2147483648
CHECK-NEXT:longint var: 2147483648
*)

program p;
var
  si: ShortInt; one_si: ShortInt;
  ii: Integer; one_i: Integer;
  li: LongInt; one_li: LongInt;
begin
  si := 127; one_si := 1;
  writeln('shortint lit: ', si + 1);
  writeln('shortint var: ', si + one_si);

  ii := 32767; one_i := 1;
  writeln('integer lit: ', ii + 1);
  writeln('integer var: ', ii + one_i);

  li := 2147483647; one_li := 1;
  writeln('longint lit: ', li + 1);
  writeln('longint var: ', li + one_li);
end.
