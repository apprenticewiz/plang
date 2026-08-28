(*
TP-only Exit (Builtins.def, reached through CallStmt exactly like Halt --
CGProcCall::emitCallStmt): inside a PROCEDURE, a bare `Exit;` returns to the
caller immediately, skipping whatever else the procedure's body would have
done -- confirmed against `fpc -Mtp`, which prints only "before exit" for
the identical program and never runs the statement after Exit.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
CHECK: before exit
CHECK-NEXT: after call
CHECK-NOT: after exit
*)

program exit_in_procedure;

procedure P;
begin
  writeln('before exit');
  Exit;
  writeln('after exit')
end;

begin
  P;
  writeln('after call')
end.
