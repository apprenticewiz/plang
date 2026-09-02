(*
Companion to exitproc-sees-the-exitcode-runerror-actually-set.pas (issue
#652): plang_halt (runtime/plang_sys.cpp) has the identical bug family --
it sets ErrorAddr before the finaliser/ExitProc chain but, before this fix,
never set ExitCode, so an ExitProc reading ExitCode after Halt(n) also saw a
stale 0 rather than n. fpc -Mtp's ExitCode after Halt(n) is n.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 42 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:about to halt
CHECK-NEXT:MyExit saw ExitCode=42
*)

program exitprochaltexitcode;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('about to halt');
  Halt(42);
  writeln('unreachable');
end.
