(*
The parser change that lets SizeOf/High/Low take a primitive type keyword
as their sole argument (Parser::parseSizeHighLowArg) is deliberately
scoped to exactly those three builtins' own first argument -- it is called
only from the CallExpr argument-parsing site in parseFactor, and only for
the FIRST argument, so a keyword type name is admitted NOWHERE else.  This
is the negative proof: `Integer` used as an ordinary expression operand
must keep failing to parse exactly as it always did, confirming the
special-case did not widen into a general "type keywords are expressions"
grammar change.

RUN: not %plang -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: expected expression, got 'integer'
*)

program p;
var
  x: Integer;
begin
  x := Integer + 1;
end.
