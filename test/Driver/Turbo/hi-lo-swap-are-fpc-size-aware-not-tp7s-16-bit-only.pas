(*
Hi/Lo/Swap here are FPC's actual `-Mtp` field practice, which is a
DELIBERATE divergence from literal Turbo Pascal 7: real TP7's Hi/Lo/Swap
only ever operated on a 16-bit value, full stop -- there was no other
integer width for them to mean anything else on.  fpc -Mtp instead sizes
all three off the argument's OWN declared width: Hi/Lo of a 16-bit value
answer in a Byte (the value's high/low BYTE), but Hi/Lo of a 32-bit value
answer in a Word (the value's high/low WORD), and Swap swaps whichever
half that width has -- a byte swap at 16 bits, a word swap at 32.  A TP7
program that Inc'd its way into a wider integer and then called Hi/Lo/Swap
on it, expecting the old always-16-bit-view meaning, gets a different
answer from this compiler (matching fpc -Mtp) than it would have from real
TP7.

Cross-checked directly against `fpc -Mtp` (3.2.2) on the exact values
below: Hi(Word(300))=1, Lo(Word(300))=44, Swap(Word(300))=11265,
Hi(LongInt(196613))=3, Lo(LongInt(196613))=5, Swap(LongInt(196613))=327683
-- fpc prints the identical six numbers for the identical six calls.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:44
CHECK-NEXT:11265
CHECK-NEXT:3
CHECK-NEXT:5
CHECK-NEXT:327683
*)

program p;
var
  w:  Word;
  li: LongInt;
begin
  w := 300;        { $012C: high byte 1, low byte 44 }
  writeln(Hi(w));
  writeln(Lo(w));
  writeln(Swap(w)); { byte-swapped: $2C01 = 11265 }

  li := 196613;     { $00030005: high word 3, low word 5 }
  writeln(Hi(li));
  writeln(Lo(li));
  writeln(Swap(li)); { word-swapped: $00050003 = 327683 }
end.
