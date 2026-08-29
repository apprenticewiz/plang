(*
Real Turbo Pascal overloads unary 'not' the same way it overloads
'and'/'or'/'xor': two's-complement bitwise negation (`not x` = `-x-1`) on an
Integer operand, logical negation on a Boolean one.  Sema::checkUnary's Not
case now accepts an Integer operand under -std=turbo (returning a plain
Integer at the OPERAND's own Width/IsSigned -- TyInt only when the operand
already was the dialect's default Integer, e.g. the untyped literals this
test uses; a sized-ladder rung like Cardinal keeps its own 32 bits instead
of being narrowed to the 16-bit default the way an earlier version of this
code unconditionally did) alongside the Boolean case ISO/EP already had;
CGBinaryOps::emitUnary tells the two apart by the operand's own
ResolvedType, since LLVM's CreateNot (xor with all-ones) is already correct
at whatever width the operand is -- unlike shl/shr, a bitwise not's result
does not depend on which width it is computed at (not(x) = -1-x is a plain
integer identity, true at any width wide enough to hold x), so no
re-coercion to the Integer's own width is needed here the way shl/shr's IS
(see shl-and-shr-*-masks-the-shift-count.pas).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-6
CHECK-NEXT:-1
CHECK-NEXT:FALSE
CHECK-NEXT:TRUE
*)

program bitwise_not;
begin
  writeln(not 5);       { two's complement: not x = -x-1, so not 5 = -6 }
  writeln(not 0);       { not 0 = -1, all bits set }
  writeln(not true);    { logical }
  writeln(not false)    { logical }
end.
