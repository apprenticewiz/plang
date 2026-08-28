(*
Real Turbo Pascal's 'xor' is overloaded the identical way 'and'/'or' are:
bitwise exclusive-or on two Integer operands, logical exclusive-or on two
Boolean ones.  Unlike and/or, xor never short-circuits under the B
directive's off state -- both operands always matter to an exclusive-or
result -- so it has no
top-of-function special case in CGBinaryOps::emitBinary to peel off first;
CreateXor is exactly right for either overload as-is, at whatever width the
operand pair shares (i1 for two Booleans, the resolved Integer width
otherwise).  Sema::checkBinary's new Xor case accepts "both Boolean" or
"both Integer" the same way And/Or's does under Turbo, and both overloads
are exercised here, not just one.

TP7's own operator table also puts 'xor' at the same precedence tier as
'or': an ADDOP, one tier below '*'/'div'/'mod'/'and'/'shl'/'shr' (the
MULOPs) -- checked directly below the same way shl-and-shr-*.pas checks
the MULOP tier: `2 * 3 xor 1` must parse as `(2 * 3) xor 1` = 7, not
`2 * (3 xor 1)` = 4, proving xor binds looser than '*' the way '+' does.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
CHECK-NEXT:false
CHECK-NEXT:true
CHECK-NEXT:7
*)

program bitwise_xor;
begin
  writeln(5 xor 3);          { bitwise: 0101 xor 0011 = 0110 = 6 }
  writeln(true xor true);    { logical: same operands, exclusive-or is false }
  writeln(true xor false);   { logical: differing operands, exclusive-or is true }
  writeln(2 * 3 xor 1)       { addop precedence: (2 * 3) xor 1 = 6 xor 1 = 7 }
end.
