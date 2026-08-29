(*
ExitProc also runs when the program block simply falls off the end,
without ever calling Halt -- emitMain's own end-of-program call to
plang_module_finals_run (CodeGenProcs.cpp) reaches the same
plang_tp_run_exitproc registered near the very start of main, so this needs
no separate "and also run ExitProc on normal exit" step of its own.  See
exitproc-runs-before-halt-terminates-the-process.pas for the Halt path.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 0 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:falling off the end
CHECK-NEXT:MyExit ran on normal exit
*)

program exitprocnormal;
procedure MyExit;
begin
  writeln('MyExit ran on normal exit');
end;
begin
  ExitProc := MyExit;
  writeln('falling off the end');
end.
