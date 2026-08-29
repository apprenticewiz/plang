(*
Tier 2 capstone: Hi/Lo/Swap at every width the ladder actually allows them
on, in one place.  These are FPC's size-aware Hi/Lo/Swap, a deliberate,
permanent divergence from literal Turbo Pascal 7 (whose versions only ever
worked on a 16-bit value) -- see docs/turbo.md's "New builtins" section.
Sema requires a real Integer-kind argument at least 16 bits wide
(err_hi_lo_swap_argument): ShortInt/Byte (8 bits) have no separate high and
low half to name, so this test only exercises Word/LongInt/Int64, the
three rungs the feature actually supports, not the full eight-wide ladder.

Hand-computed (Hi/Lo answer in an UNSIGNED integer half the argument's own
width; Swap rotates by half the width and keeps the argument's own type):
  Word    $1234        -> Hi=$12=18, Lo=$34=52, Swap=$3412=13330
  LongInt $12345678     -> Hi=$1234=4660, Lo=$5678=22136,
                            Swap=$56781234=1450709556 (fits signed 32-bit)
  Int64   $123456789ABCDEF0 -> Hi=$12345678=305419896 (LongWord),
                                Lo=$9ABCDEF0=2596069104 (LongWord),
                                Swap=$9ABCDEF012345678, which as a SIGNED
                                64-bit value (Swap keeps Int64's own
                                signed type) is -7296712173568108936.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:18 52 13330
CHECK-NEXT:4660 22136 1450709556
CHECK-NEXT:305419896 2596069104 -7296712173568108936
*)

program hi_lo_swap_ladder;
var
  w:  Word;
  li: LongInt;
  i6: Int64;
begin
  w := $1234;
  writeln(Hi(w), ' ', Lo(w), ' ', Swap(w));

  li := $12345678;
  writeln(Hi(li), ' ', Lo(li), ' ', Swap(li));

  i6 := $123456789ABCDEF0;
  writeln(Hi(i6), ' ', Lo(i6), ' ', Swap(i6));
end.
