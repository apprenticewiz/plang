(*
Issue #631: Hi/Lo/Swap keyed the rotation/shift width off their argument's
OWN LLVM type (`v->getType()`) rather than off Sema's resolved width -- fine
for a VARIABLE argument, whose LLVM type already matches its declared
Width throughout codegen, but wrong for an integer LITERAL argument: every
integer literal lowers to a 64-bit LLVM ConstantInt regardless of its
resolved (narrower) type (CGExprCore::emitExpr's IntLitExpr case), so
`Swap($1234)` -- a literal Sema resolves as a 16-bit Turbo Integer -- had
its rotation computed at 64 bits instead of 16, and `Hi`/`Lo` truncated
after an unmasked 32-bit shift instead of the correct 8-bit one.

Cross-checked against `fpc -Mtp` (3.2.2): Swap($1234) = 13330,
Hi($1234) = 18, Lo($1234) = 52 -- plang used to print 20014547599360, 0 and
4660 instead ($1234 shl 32 unmasked, and $1234's own low/high halves read
at the wrong split point). Shl/Shr (see this directory's own
shl-shr-compute-at-the-shifted-operands-own-width-not-the-dialect-default.pas)
hit the identical literal-width trap for the identical reason and were
fixed the same way: re-coerce to the resolved width before computing,
rather than trusting whatever width the operand's LLVM value arrived in.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:13330
CHECK-NEXT:18 52
*)

program hi_lo_swap_literal;
begin
  writeln(Swap($1234));
  writeln(Hi($1234), ' ', Lo($1234));
end.
