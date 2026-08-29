(*
Turbo string[N] semantics item, concrete work 1, at the Sema layer: unlike
EP's string(N) (Sema::checkStringCapacity's own err_string_too_long arm),
assigning a literal longer than a ShortString's declared capacity produces
NO diagnostic at all -- checkStringCapacity's ShortString early return
(SemaStmt.cpp) and isAssignCompatible's ShortString Kind-equality/String
cross-kind rules (SemaExpr.cpp) both have to agree on this, or the program
below would either fail to compile at all (isAssignCompatible rejecting the
pairing) or wrongly report the EP capacity error (checkStringCapacity not
recognizing ShortString as a destination that truncates instead).  The
actual TRUNCATING behavior this accepts into is CodeGen's job and has its
own test (shortstring-assignment-truncates-instead-of-erroring.pas,
test/CodeGen/Turbo) -- this one is purely "Sema raises nothing".

RUN: %plang -std=turbo -dump-ast %s > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

program p;
var s: string[3];
begin
  s := 'a value much longer than three characters';
  writeln('ok')
end.

(*
CHECK-NOT: error:
CHECK: (assign
*)
