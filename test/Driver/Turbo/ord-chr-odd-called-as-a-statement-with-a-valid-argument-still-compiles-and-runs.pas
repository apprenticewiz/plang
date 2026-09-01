(*
issue #547's fix wires Sema::checkBuiltinArgKinds into checkCallStmt's own
generic fallback -- the same call checkCallExpr already makes for every
ArgKind-migrated builtin (ord/chr/odd's AK_Ordinal, sqrt/abs's AK_Numeric,
...).  A VALID argument must still compile and run exactly as before: this
is the false-positive-regression check the sibling
ord/chr/odd-rejects-a-non-ordinal-argument-called-as-a-statement.pas files
(test/CodeGen/OrdChrOdd) don't cover, since those are all rejections.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

{$X+}
begin
  ord(65);
  chr(65);
  odd(3);
  sqrt(4.0);
  writeln('ok');
end.

(*
CHECK:ok
*)
