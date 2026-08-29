(*
Integer(SomeReal) is a legal VALUE typecast (both ordinal-or-real), but it
is not a legal assignment TARGET: Real and Integer are different sizes, so
isLValue's TypeCastExpr case (SemaExpr.cpp) refuses it as storage to write
through even though its operand, SomeReal, is itself a variable.  This is
the same "read a value, but that value has no place of its own to write
back into" refusal an ordinary function call already gets
(err_lhs_not_lvalue) -- not the size-mismatch diagnostic
(err_invalid_type_cast) SemaTurboTypeCast's other test covers, since the
cast itself is perfectly legal as a value.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: left-hand side of assignment is not an assignable variable
*)

program p;
var
  R: Real;
begin
  R := 3.5;
  Integer(R) := 5;
end.
