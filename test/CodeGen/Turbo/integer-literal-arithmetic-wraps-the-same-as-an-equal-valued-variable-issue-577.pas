(*
issue #577: every IntLitExpr was codegen'd as an i64 LLVM constant
(CGExprCore.cpp) no matter what its own Sema-resolved type's width
actually was.  CGBinaryOps::emitBinary's operand-width-unification then
always promotes to whichever operand's LLVM type is WIDER -- so mixing a
narrow Turbo Integer (16-bit) variable with ANY integer literal silently
promoted the whole add/comparison to 64 bits, which never wraps at
Integer's declared width, while the same arithmetic against a second
16-bit VARIABLE holding the identical value stayed narrow and wrapped
correctly. The same two summands (30000 + 30000) printed two different
answers -- and, worse, the same comparison (`(a + b) > 0`) took OPPOSITE
branches -- purely depending on whether the second operand was written as
a literal or as a variable.

Fixed by emitting an integer literal's LLVM constant at its own
Sema-resolved width (falling back to i64 only when no resolved type is
available) instead of unconditionally at i64, so a literal now
participates in width-unification on the same footing as a variable of
its own type -- matching the checked-in expectation below, that `a + b`
and `a + <literal equal to b>` behave identically for both value and
control flow.

The Byte case checks a related, easily-conflated but DIFFERENT
invariant: a literal's own Sema-resolved type is always the dialect's
plain Integer (16-bit under Turbo), never the narrower type of whatever
it happens to be combined with, so `Byte + literal(200)` is genuinely
Integer-typed arithmetic (commonIntType(Byte, Integer) = Integer) --
NOT the same expression as `Byte + Byte`, which stays 8-bit and wraps.
What issue #577's fix guarantees is that a literal behaves like a
VARIABLE of its own (Integer) type, not like a variable of whatever
type it is paired with -- so `by1 + 200` must match `by1 + <an Integer
variable holding 200>` exactly, which this checks.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:add: -5536 -5536
CHECK-NEXT:cmp: NEG NEG
CHECK-NEXT:byte: 400 400
*)

program p;
var
  a, b: Integer;
  by1: Byte;
  iv: Integer;
begin
  a := 30000; b := 30000;
  writeln('add: ', a + b, ' ', a + 30000);
  if (a + b) > 0 then write('cmp: POS ') else write('cmp: NEG ');
  if (a + 30000) > 0 then writeln('POS') else writeln('NEG');
  by1 := 200; iv := 200;
  writeln('byte: ', by1 + 200, ' ', by1 + iv);
end.
