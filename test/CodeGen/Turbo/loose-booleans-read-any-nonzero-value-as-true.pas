(*
ByteBool/WordBool/LongBool are Turbo's "loose" Boolean-family widths
(Type::IsLooseBool, Sema/Type.h): unlike strict Boolean, ANY nonzero value
is true, not just the literal 1 -- confirmed against real fpc -Mtp (a
typecast is fpc's only way to put a non-canonical value in one; plang has no
typecast yet, so 'absolute' overlays the same storage instead, which is
already shipped Tier 2).  This is the behavior the whole feature exists for:
`var b: ByteBool; b := <some byte-shaped 200>; if b then` must print true,
and reading the byte back must still show 200 -- not 1, and not silently
false.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:200 true
CHECK-NEXT:40000 true
CHECK-NEXT:70000 true
CHECK-NEXT:false
*)

var
  b: ByteBool;
  w: WordBool;
  l: LongBool;
  rawB: Byte    absolute b;
  rawW: Word    absolute w;
  rawL: LongInt absolute l;
begin
  // A conventional assignment first, so the overlay writes below are not
  // this variable's only definite assignment as far as the flow checker
  // (SemaFlow.cpp) can tell -- it does not know 'rawB'/b share storage,
  // only that b itself was given a value before being read.
  b := false; w := false; l := false;

  rawB := 200;
  write(rawB, ' ');
  if b then writeln('true') else writeln('false');

  rawW := 40000;
  write(rawW, ' ');
  if w then writeln('true') else writeln('false');

  rawL := 70000;
  write(rawL, ' ');
  if l then writeln('true') else writeln('false');

  rawB := 0;
  if not b then writeln('false');
end.
