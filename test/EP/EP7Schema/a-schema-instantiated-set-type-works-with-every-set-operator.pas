(*
Issue #584: a schema whose body is a set type (`type s(n: integer) = set of
1..n;`) can be declared and assigned to, but every set operator used to
reject it -- 'in', '+', '=', '<>', 'card' and 'for...in' all compared the
RAW type Kind against TypeKind::Set without first unwrapping a schema
wrapper through schemaUnderlying (Sema/Type.h) the way Record and VarString
already were.  Each call site (checkBinary's Times/SymDiff/comparison/In
arms, checkBuiltinCall's card arm, checkForIn -- all SemaExpr.cpp/
SemaStmt.cpp) now takes that same hop first, mirroring CodeGen's own
schemaUnderlying calls in SetOps.cpp/CGBinaryOps.cpp/CGControlFlow.cpp.

RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in
CHECK-NEXT:5
CHECK-NEXT:3
CHECK-NEXT:1
CHECK-NEXT:3
CHECK-NEXT:5
CHECK-NEXT:eq
CHECK-NEXT:ne
CHECK-NEXT:union has 4
*)

program p;
type s(n: integer) = set of 1..n;
var x, y, z: s(5); i: integer;
begin
  x := [1, 3, 5];
  y := [2, 3];
  if 3 in x then writeln('in');
  writeln(x.n);
  writeln(card(x));
  for i in x do writeln(i);
  if x = [1, 3, 5] then writeln('eq');
  if x <> y then writeln('ne');
  z := x + y;
  writeln('union has ', card(z))
end.
