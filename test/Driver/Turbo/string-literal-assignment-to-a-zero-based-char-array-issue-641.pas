(*
Issue #641: a string literal assigned to a 0-based `array[0..N] of char`
was rejected outright ("cannot assign 'string' to variable of type
'array[0..9] of char'"), even though the same array already decays to a
PChar the other direction (`p := buf`, see
test/CodeGen/Turbo/pchar-pointer-arithmetic-indexing-and-array-decay.pas)
and `fpc -Mtp` accepts the literal assignment. Sema::isAssignCompatible
(SemaExpr.cpp) now has the mirror-image rule, gated on -std=turbo the same
"IndexType->SubLo == 0" structural way as the PChar-decay rule.

CGAssign's own store for this pairing matches `fpc -Mtp` field practice
exactly, confirmed empirically against a local build: a literal at least
as long as the array is copied TRUNCATED to fit, with no terminator
written; a shorter one is copied and every remaining byte of the array is
explicitly zero-filled, not left holding whatever it held before.

See test/Sema/SemaTurboPChar/a-one-based-char-array-does-not-decay-to-pchar.pas
for the still-unaffected 1-based (isCharStringType, ISO §6.4.3.2) sibling
case, which this does not touch.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:104 105 0 0 0 0 0 0 0 0
CHECK-NEXT:104 101 108 108 111 119 111 114 108 100
*)

program p;
var
  buf: array[0..9] of Char;
  i: Integer;
begin
  for i := 0 to 9 do buf[i] := '#';
  buf := 'hi';
  for i := 0 to 9 do write(Ord(buf[i]), ' ');
  writeln;

  buf := 'helloworldXXX';  { longer than the array: truncated to fit }
  for i := 0 to 9 do write(Ord(buf[i]), ' ');
  writeln;
end.
