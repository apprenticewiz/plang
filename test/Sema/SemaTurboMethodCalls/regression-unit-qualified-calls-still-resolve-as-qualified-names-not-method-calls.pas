(*
Turbo Tier 5, Cluster A item 3 regression check: 'Crt.ClrScr' (a `uses Crt`
unit qualified by name -- Tier 4's own EP §6.11.2-style qualification) has
to keep resolving the way it always has, NOT get reinterpreted as a method
call now that '.identifier(' can build a MethodCallExpr.  It is not
affected because Parser::parseFactor's QualifiedModules_ check consumes the
'.' and folds "Crt.ClrScr" into one dotted CallExpr::Name BEFORE
parsePostfix's own Dot-handling (where MethodCallExpr is built) ever sees
it -- confirmed here by the dump-ast still showing an ordinary
'(call Crt.ClrScr)', not a methodcall node.
*)

(*
RUN: %plang_ir -std=turbo -dump-ast %s | FileCheck %s
*)

program UnitQualifiedRegression;
uses Crt;
begin
  Crt.ClrScr;
  ClrScr;
end.

(*
CHECK: (call Crt.ClrScr)
CHECK-NEXT: (call ClrScr)
*)
