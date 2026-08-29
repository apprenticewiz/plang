(*
ErrorAddr's second (of exactly two) tracked fault sites: plang_halt sets it
for a NONZERO status only -- Halt(0) is an ordinary successful exit, not a
reported error, and leaves ErrorAddr nil.  See
erroraddr-is-observable-from-inside-a-runtime-errors-own-exitproc-
handler.pas for the RunError site, and
erroraddr-is-nil-until-a-runtime-error-sets-it.pas for the "never touched"
baseline.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 7 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:in ExitProc: non-nil
*)

program erroraddrhaltnonzero;
procedure ReportErr;
begin
  if ErrorAddr <> nil then writeln('in ExitProc: non-nil')
  else writeln('in ExitProc: FAIL, nil');
end;
begin
  ExitProc := ReportErr;
  Halt(7);
end.
