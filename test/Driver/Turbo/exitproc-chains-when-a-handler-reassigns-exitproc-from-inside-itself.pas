(*
Issue #595: real Turbo Pascal's ExitProc mechanism supports chaining -- a
documented idiom (TSRs, library units, cleanup chains) where a handler
installs itself, saves the previous ExitProc value, and assigns a NEW
ExitProc from inside its own body; the exit sequence is a LOOP that keeps
consuming and calling whatever ExitProc currently holds, so the
newly-assigned handler runs too -- confirmed against a local `fpc -Mtp`
3.2.2 install.  plang_tp_run_exitproc (runtime/plang_sys.cpp) used to call
the installed handler exactly once, clearing ExitProc first but never
re-checking whether the handler itself assigned a new one before returning,
so a second, chained handler silently never ran.  See
exitproc-runs-on-normal-program-termination.pas for the simpler,
non-chaining case this must keep working exactly as before.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 0 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:main body done
CHECK-NEXT:first handler ran
CHECK-NEXT:second handler ran
CHECK-NEXT:third handler ran
*)

program exitprocchain;

procedure Third;
begin
  writeln('third handler ran');
end;

procedure Second;
begin
  writeln('second handler ran');
  ExitProc := Third;
end;

procedure First;
begin
  writeln('first handler ran');
  ExitProc := Second; { chain: assign a NEW ExitProc from inside the running one }
end;

begin
  ExitProc := First;
  writeln('main body done');
end.
