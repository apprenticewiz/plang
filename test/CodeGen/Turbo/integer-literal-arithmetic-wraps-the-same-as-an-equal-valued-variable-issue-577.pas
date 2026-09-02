(*
issue #577 (reopened -- see this test's own comment history and
CGBinaryOps.cpp's emitBinary Plus/Minus/Times comment for the full
derivation): every IntLitExpr was codegen'd as an i64 LLVM constant
(CGExprCore.cpp) no matter what its own Sema-resolved type's width
actually was.  CGBinaryOps::emitBinary's operand-width-unification then
always promoted to whichever operand's LLVM type was WIDER -- so mixing a
narrow Turbo Integer (16-bit) variable with ANY integer literal silently
promoted the whole add/comparison to 64 bits, which never wraps at
Integer's declared width, while the same arithmetic against a second
16-bit VARIABLE holding the identical value stayed narrow and wrapped
correctly. The same two summands (30000 + 30000) printed two different
answers -- and, worse, the same comparison (`(a + b) > 0`) took OPPOSITE
branches -- purely depending on whether the second operand was written as
a literal or as a variable.

A first fix (PR #756) narrowed a literal operand's promotion width to
match, which made `a + b` and `a + <literal>` agree -- but only by making
BOTH of them wrap early (at Integer's 16-bit width), which does not match
real `fpc -Mtp` field practice: an UNSTORED expression (a bare write
argument, a comparison operand, anything not going through an assignment)
never wraps at all there, no matter whether either operand is a literal or
a variable -- `Writeln(a + b)` for two Integer(30000) prints 60000, not a
wrapped -5536.  Confirmed against a real local `fpc -Mtp` build for every
row this test checks. `fpc`'s own code generator only ever narrows a
result on STORE into a fixed-width l-value (see the STORED-case sibling
test, integer-and-byte-arithmetic-only-wraps-on-store-into-a-fixed-width-
variable-issue-577.pas, for that half of the behavior) -- CGBinaryOps.cpp's
emitBinary now matches that: Plus/Minus/Times compute at full i64
precision from each operand's own raw value, and rely on the ALREADY-
EXISTING narrowing at the consuming site (CGAssign's store, or
StringCallMarshalling::emitCallArg's value-parameter coercion) to narrow
back down whenever one actually exists.

The Byte case checks a related, easily-conflated but DIFFERENT invariant:
a literal's own Sema-resolved type is always the dialect's plain Integer
(16-bit under Turbo), never the narrower type of whatever it happens to be
combined with, so `Byte + literal(200)` is genuinely Integer-typed
arithmetic (commonIntType(Byte, Integer) = Integer) -- NOT the same
EXPRESSION TYPE as `Byte + Byte`.  Both still compute and print the exact
same VALUE here (400), because neither one is stored anywhere -- see the
sibling STORED test for the case where that distinction (Integer- vs.
Byte-width truncation) actually bites.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:add: 60000 60000
CHECK-NEXT:cmp: POS POS
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
