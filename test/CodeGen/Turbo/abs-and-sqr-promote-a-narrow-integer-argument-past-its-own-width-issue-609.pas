(*
issue #609: CGFuncCall::emitBuiltinCall's Abs/Sqr codegen called
plang_abs_int/plang_sqr_int -- both always i64(i64) -- and returned that
i64 result as-is, exposing the runtime HELPER's own width instead of the
semantically correct one.  A prior version of this fix assumed "correct"
meant "the argument's own type" (Sema's checkCallExpr comment used to say
"abs/sqr are polymorphic: return the argument's type" and CodeGen narrowed
to match), but that does NOT match real `fpc -Mtp` field practice: e.g.
`Sqr(Word(60000))` prints a NEGATIVE 32-bit result there, not a
Word-shaped positive 16-bit wraparound.

Real fpc's actual rule, confirmed empirically against `fpc -Mtp` for every
row below: any Turbo sized-integer rung NARROWER than 32 bits (ShortInt,
Byte, Integer, Word) computes and returns Abs/Sqr at 32-bit SIGNED
(LongInt) regardless of its own original signedness -- LongInt's own range
already holds every value an 8- or 16-bit rung, signed or not, can
produce, so fpc discards the narrower sign entirely rather than preserve
it.  The one rung this does NOT cover is Cardinal/LongWord (32-bit
UNSIGNED): its own full range does not fit inside a same-width SIGNED
LongInt, so it promotes to 64-bit UNSIGNED (QWord) instead.  LongInt
itself keeps its own 32-bit width (silently wrapping on overflow, same as
every other Turbo integer op).  A Single argument is the one case that DOES
keep its own (narrower-than-the-helper's-double) type -- Sqr(Single) must
round its result through binary32 precision, not stay at the helper's full
double precision.  See Sema/SemaExpr.cpp's "abs/sqr are polymorphic" arm
and CodeGen/CGFuncCall.cpp's absSqrTargetIntTy for the two matching
implementations of this table.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:shortint abs: 128
CHECK-NEXT:shortint sqr: 400
CHECK-NEXT:byte sqr: 400
CHECK-NEXT:integer abs: 32768
CHECK-NEXT:integer sqr: 40000
CHECK-NEXT:word sqr: 40000
CHECK-NEXT:longint abs: -2147483648
CHECK-NEXT:longint sqr: 605032704
CHECK-NEXT:cardinal sqr: 4900000000
CHECK-NEXT:single sqr: 16785408.0
*)

program p;
var
  si: ShortInt; by: Byte;
  i: Integer; w: Word;
  li: LongInt; c: Cardinal;
  sg: Single;
begin
  si := -128; writeln('shortint abs: ', abs(si));
  si := 20;   writeln('shortint sqr: ', sqr(si));
  by := 20;   writeln('byte sqr: ', sqr(by));

  { Integer is 16-bit signed under -std=turbo, so MinInt's abs (32768)
    does not fit back into 16 bits -- a strong signal that Abs really did
    promote to 32-bit LongInt rather than truncate back to Integer. }
  i := -32768; writeln('integer abs: ', abs(i));
  i := 200;    writeln('integer sqr: ', sqr(i));
  { 200*200 = 40000 does not fit a 16-bit SIGNED type (max 32767) either,
    so a Word-shaped (even if unsigned) 16-bit result would have to wrap;
    printing 40000 confirms the 32-bit LongInt promotion, not a 16-bit
    unsigned one. }
  w := 200;    writeln('word sqr: ', sqr(w));

  li := -2147483648; writeln('longint abs: ', abs(li));
  li := 70000;        writeln('longint sqr: ', sqr(li));
  { 70000*70000 = 4900000000 overflows 32-bit LongInt (both signed and
    unsigned readings), so an unpromoted 32-bit sqr would have wrapped;
    printing the exact value confirms Cardinal's own promotion to 64-bit
    QWord. }
  c := 70000;          writeln('cardinal sqr: ', sqr(c));

  sg := 4097; writeln('single sqr: ', sqr(sg):0:1)
end.
