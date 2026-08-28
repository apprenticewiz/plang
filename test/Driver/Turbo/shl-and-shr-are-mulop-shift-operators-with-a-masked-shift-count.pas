(*
Turbo's shl/shr have no ISO/EP equivalent at all -- ParseExpr.cpp's isMulop
now includes them (TP7's own operator table puts shl/shr at the same
precedence tier as '*', unlike and/or which the table would have as
mulops too, or or/xor's addop tier), and Sema::checkBinary's new Shl/Shr
case is integer-only under -std=turbo (no Boolean overload the way
and/or/xor/not have one -- shifting a Boolean makes no sense and TP does
not define it).

The mulop-tier precedence is checked directly: `2 + 1 shl 2` must parse as
`2 + (1 shl 2)` = 6, not `(2 + 1) shl 2` = 12, proving shl binds tighter
than '+' the same way '*' does.

The shift count is masked to (width-1) before reaching LLVM's shl/lshr,
which are POISON -- not merely wrong -- if the shift amount is >= the
operand's bit width.  `1 shl 20` on Turbo's 16-bit Integer must not pass 20
straight through: masked, the count is 20 and 15 = 4, so `1 shl 20` = `1
shl 4` = 16, which this test confirms is the ACTUAL answer (not just "does
not crash").  CGBinaryOps.cpp's Shl/Shr case derives the width to mask
against from the expression's own Sema-resolved type (e.ResolvedType->
Width) rather than from whatever LLVM type the operands' own codegen
happened to produce -- an integer literal always lowers to an i64
ConstantInt regardless of dialect (CGExprCore::emitExpr), so trusting the
operand's LLVM type here would have silently computed an unmasked 64-bit
shift for `1 shl 20` instead of a masked 16-bit one.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:16
CHECK-NEXT:16
CHECK-NEXT:6
CHECK-NEXT:16
*)

program shift_ops;
begin
  writeln(1 shl 4);        { 1 * 2^4 = 16 }
  writeln(256 shr 4);      { 256 / 2^4 = 16 }
  writeln(2 + 1 shl 2);    { mulop precedence: 2 + (1 shl 2) = 2 + 4 = 6 }
  writeln(1 shl 20)        { count masked to 20 and 15 = 4: 1 shl 4 = 16 }
end.
