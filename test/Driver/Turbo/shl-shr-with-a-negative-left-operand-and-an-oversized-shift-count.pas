(*
Tier 2 capstone: 'shl'/'shr' with a negative left operand and a shift count
past the operand's own bit width, in one place.  LLVM's shl/lshr are POISON
(not merely wrong) once the shift amount reaches the operand's own bit
width, so CGBinaryOps masks the count to (width-1) first -- the same thing
a real hardware shift instruction (and FPC's own codegen) does -- and 'shr'
is always a LOGICAL shift even on a signed Integer (CreateLShr, never
CreateAShr): a negative operand's top bit fills with zero, not carried, so
'shr' does NOT behave like a power-of-two 'div' here.

Traced by hand for Turbo's own 16-bit Integer (mask = width-1 = 15):
  i := -1  is bit pattern 0xFFFF.
  i shl 20: count = 20 and 15 = 4.  0xFFFF shl 4, truncated to 16 bits,
            is 0xFFF0 = -16 as a signed 16-bit value.
  i shr 20: count = 4.  0xFFFF lshr 4 (logical, zero-filled from the top)
            is 0x0FFF = 4095 -- positive, confirming this is NOT an
            arithmetic (sign-preserving) shift.

NOTE (a genuine finding, not exercised further here -- see this PR's own
description): this same masked-shift codegen re-coerces its operands to
e.ResolvedType->Width, and Sema::checkBinary's Shl/Shr arm unconditionally
returns TyInt (the dialect's own default Integer width, 16 bits under
Turbo) as a shl/shr expression's type regardless of the ACTUAL operand
type's width -- unlike Div/Mod, whose own width-generic fix
(RangeCheckGuards, issue threading the real operand width through) this
mirrors in comment only, not in effect. So 'shl'/'shr' on a LongInt/Int64/
Byte/etc. operand silently computes at 16 bits instead of that type's own
width once a shift count or a top bit reaches past 16.  This test
deliberately stays within plain Integer, the one width where
e.ResolvedType's assumed width and the operand's actual width agree, and
does not attempt to exercise a wider or narrower rung here.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-16
CHECK-NEXT:4095
*)

program shl_shr_edge_cases;
var
  i: Integer;
begin
  i := -1;
  writeln(i shl 20);
  writeln(i shr 20);
end.
