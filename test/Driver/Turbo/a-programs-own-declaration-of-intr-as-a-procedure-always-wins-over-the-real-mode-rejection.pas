(*
Same as Mem's own regression test (a-programs-own-declaration-of-mem-...),
but for the STATEMENT-call hook point: Intr is ordinarily called as a
procedure statement in real Turbo Pascal (`Intr($21, Regs);`), which reaches
checkCallStmt (SemaStmt.cpp) rather than checkIdent/checkCallExpr
(SemaExpr.cpp).  A program that declares its own procedure named Intr must
resolve to that declaration there too, and the call must actually run --
compiled and executed, not just type-checked.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
procedure Intr(a, b: integer);
begin
  writeln(a + b)
end;
begin
  Intr(3, 4)
end.
