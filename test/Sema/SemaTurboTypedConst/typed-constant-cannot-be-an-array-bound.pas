(*
TP7's typed constant is a variable with static storage, not a true
constant (see typed-constant-persists-across-calls.pas), so it may not
stand as an array bound the way a real 'const' can.  Sema delivers this for
free by design: a typed constant is registered as a SymbolKind::Var
(Symbol::IsTypedConst), never a SymbolKind::Const -- and Sema::constBound
(SemaType.cpp) only folds a SymbolKind::Const with HasConstOrdinal, so a Var
is simply never a constant expression, with no dedicated rejection logic of
its own.  This is the regression gate for that design actually holding.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: upper bound of array index type is not a constant expression
*)

const Bound: Integer = 5;
var A: array[1..Bound] of Integer;
begin
  writeln(A[1]);
end.
