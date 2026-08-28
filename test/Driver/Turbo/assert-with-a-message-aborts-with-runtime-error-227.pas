(*
Turbo's Assert(cond, msg) (Builtins.def, gated on Switch::Assertions,
CGProcCall::emitCallStmt), with assertions at their Turbo default (ON --
CompilerSwitches.def's TurboDefault for Assertions -- so this needs no
{$C+} of its own): a false condition raises RTE 227, plang's
runtime/plang_sys.cpp reporter naming the number and carrying the message
through, matching the shape `fpc -Mtp` itself reports a failed assertion
in (confirmed empirically: "boom (t.pas, line N)." and process exit 227).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before the assertion
ERR: Runtime error 227
ERR: boom
*)

program assert_with_message;
begin
  writeln('before the assertion');
  Assert(false, 'boom');
  writeln('unreachable')
end.
