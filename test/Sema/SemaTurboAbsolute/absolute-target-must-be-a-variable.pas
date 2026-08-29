(*
'absolute' aliases a new variable onto existing STORAGE, so its target has
to be addressable -- isLValue (SemaExpr.cpp), the same requirement checked
for a 'var' parameter argument or the operand of Turbo's own '@' operator.
A real constant (SymbolKind::Const, not a typed one -- see
../SemaTurboTypedConst) names a value, not storage, and is refused the same
way.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'absolute' must name a variable or a component of one
*)

const NotAVariable = 5;
var W: Integer absolute NotAVariable;
begin
  writeln(W);
end.
