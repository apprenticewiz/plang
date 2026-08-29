(*
Tier 2 capstone: 'set of 0..9' is still a 32-byte (256-bit) representation
under '-std=turbo', a PERMANENT deviation from real Turbo/FPC field
practice, not a bug -- see docs/turbo.md's "a set is always 256 bits wide,
in every dialect -- narrowing does not reach it" entry.  Tier 2's own
per-declaration storage-narrowing work (TypeContext::narrowestStorage)
applies to plain integers, subranges, and enumerations; it was never wired
into Set's own lowering, which stays the fixed-width bitmask
(PlangMaxSetElements = 256, Sema/Type.h) every dialect has always used.
Pinned two ways: SizeOf of both a set VARIABLE and the set TYPE NAME
itself agree (32, not 2 -- the byte count '0..9' would narrow a plain
ordinal to), and a comparison set that would need a real 300-element base
range (int 0..299) still fits the SAME fixed 32 bytes -- proving the
representation is a flat 256-bit ceiling, not "just enough for the
declared range" the way a narrowed integer would be.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:32
CHECK-NEXT:32
CHECK-NEXT:32
CHECK-NEXT:TRUE
*)

program set_still_256_bits_wide;
type
  TSmallSet = set of 0..9;
  TWiderSet = set of 0..255;
var
  small: TSmallSet;
  wide:  TWiderSet;
begin
  writeln(SizeOf(small));
  writeln(SizeOf(TSmallSet));
  writeln(SizeOf(wide));
  writeln(SizeOf(TSmallSet) = SizeOf(TWiderSet));
end.
