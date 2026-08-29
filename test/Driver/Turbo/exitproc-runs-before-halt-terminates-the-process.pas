(*
ExitProc (Sema::registerBuiltins, -std=turbo only) -- a settable
procedural-value predefined variable hooked into the ALREADY-WORKING
plang_module_finals_run/plang_halt chain (issue #242).  Confirms the
program's own assigned handler actually runs, with ExitCode already
holding whatever the program set before halting, before Halt's own status
becomes the process's real exit code.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 5 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:about to halt
CHECK-NEXT:MyExit ran
*)

program exitprochalt;
procedure MyExit;
begin
  writeln('MyExit ran');
end;
begin
  ExitProc := MyExit;
  writeln('about to halt');
  Halt(5);
  writeln('unreachable');
end.
