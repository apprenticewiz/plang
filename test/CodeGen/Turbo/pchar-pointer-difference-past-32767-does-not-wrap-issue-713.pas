(*
Issue #713: p1 - p2 (both PChar) used to be typed TyInt -- Turbo's own
16-bit signed Integer -- so a genuine span longer than +/-32767 wrapped
through that width before ever reaching the destination variable, however
wide the destination itself was declared (LongInt does not help: the
subtraction is already wrapped by the time it is assigned).  fpc -Mtp
answers a real 40000-Char span with 40000, unwrapped, both assigned to a
LongInt and written directly -- confirmed empirically -- because it types
p1 - p2 as Longint (32-bit signed), not its own 16-bit Integer.
Sema::checkBinary now returns that same LongInt for this case
(Ctx_.getInt(32, true) instead of TyInt), and CGBinaryOps.cpp's own
p1-p2 codegen already derived its result width from e.ResolvedType rather
than hardcoding one, so the fix is Sema-only.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:40000
CHECK-NEXT:40000
*)

var
  buf: array[0..40000] of Char;
  a, b: PChar;
  d: LongInt;
begin
  a := @buf[0];
  b := @buf[40000];
  d := b - a;
  writeln(d);
  writeln(b - a);
end.
