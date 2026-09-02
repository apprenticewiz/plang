(*
Issue #648: a call through a procedural-typed ARRAY ELEMENT ('a[i](args)')
did not even PARSE -- the statement parser's Lval-to-statement conversion
(Parser::finishLvalueStatement) recognized only an IdentExpr, a
MethodCallExpr and a bare FieldExpr as call shapes, and Parser::parsePostfix
never consumed a '(' at all, so an IndexExpr (or DerefExpr) root fell
straight through to "expected ':=' after variable reference" as a
statement and "expected ')', got '('" as an expression.  The only
workaround was to copy the element into a temporary procedural variable
first.

Parser::parsePostfix now also consumes a trailing '(' once the postfix
chain built so far is no longer a bare identifier (Turbo procedural
VALUES' new IndirectCallExpr/IndirectCallStmt node pair), reached both
through an array ELEMENT and through a DEREFERENCED POINTER to a
procedural value, in both statement position (result discarded) and
expression position (result used in an arithmetic operand, forcing a
value context -- a bare 'x := f(...)' alone would not distinguish this
from the pre-existing, unrelated "bare procedural variable auto-calls"
path, issue #649).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:h1:11
CHECK-NEXT:h2:22
CHECK-NEXT:viaindex:26
CHECK-NEXT:viaptr:37
*)

program IndirectCallThroughIndexAndDeref;

type
  TProc = procedure(v: Integer);
  TFunc = function(v: Integer): Integer;

var
  handlers: array[1..2] of TProc;
  funcs: array[1..1] of TFunc;
  sq: TFunc;
  sqPtr: ^TFunc;
  total: Integer;

procedure H1(v: Integer);
begin
  Writeln('h1:', v);
end;

procedure H2(v: Integer);
begin
  Writeln('h2:', v);
end;

function Square(v: Integer): Integer;
begin
  Square := v * v;
end;

begin
  handlers[1] := H1;
  handlers[2] := H2;
  { statement position, through an array element }
  handlers[1](11);
  handlers[2](22);

  { expression position, through an array element }
  funcs[1] := Square;
  total := funcs[1](5) + 1;
  Writeln('viaindex:', total);

  { expression position, through a dereferenced pointer }
  sq := Square;
  sqPtr := @sq;
  Writeln('viaptr:', sqPtr^(6) + 1);
end.
