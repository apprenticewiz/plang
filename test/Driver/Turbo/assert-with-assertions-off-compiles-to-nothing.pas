(*
The other half of Assert's contract: {$C-} (Switch::Assertions off) makes
the whole call compile to nothing -- not even evaluating cond -- confirmed
against real `fpc -Mtp` field practice, which never runs a side-effecting
condition's side effect once assertions are off.  SideEffect below writes
to stdout right before returning false; if CGProcCall::emitCallStmt's
"check Switch::Assertions before emitting anything at all, including the
call to evaluate cond" ever regressed into "emit the call and let the
runtime reporter no-op instead," this would print "side effect ran" and
still succeed -- so this checks NOT ONLY the exit code and final output,
but that the side effect's own line never appears at all.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

(*
CHECK-NOT: side effect ran
CHECK: after the assertion
*)

program assert_assertions_off;
function SideEffect: Boolean;
begin
  writeln('side effect ran');
  SideEffect := false
end;
begin
  {$C-}
  Assert(SideEffect, 'should never be seen');
  writeln('after the assertion')
end.
