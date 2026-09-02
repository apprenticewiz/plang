(*
issue #652: plang_tp_runerror (runtime/plang_sys.cpp) set ErrorAddr before
running the finaliser/ExitProc chain, but never set ExitCode -- an ExitProc
that reads ExitCode to log why the program is exiting saw a stale 0 even
though the process's own exit status was correctly 99.  fpc -Mtp's ExitCode
after RunError(n) is n, exactly like Halt(n)'s; plang_tp_runerror now sets
it before the same chain plang_halt already did (this file's sibling test,
exitproc-runs-before-halt-terminates-the-process.pas).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 99 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:about to runerror
CHECK-NEXT:MyExit saw ExitCode=99
*)

program exitprocrunerror;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('about to runerror');
  RunError(99);
  writeln('unreachable');
end.
