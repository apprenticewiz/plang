(*
ErrorAddr is set BEFORE the plang_module_finals_run/ExitProc chain runs
(runtime/plang_sys.cpp's plang_tp_runerror's own comment), specifically so
a custom ExitProc -- real Turbo Pascal field practice's most common reason
to read ErrorAddr at all -- sees the right (non-nil) value from inside its
own call, even though the process is already unwinding toward exit.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 42 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before: nil
CHECK-NEXT:in ExitProc: non-nil
*)

program erroraddrinexitproc;
procedure ReportErr;
begin
  if ErrorAddr <> nil then writeln('in ExitProc: non-nil')
  else writeln('in ExitProc: FAIL, nil');
end;
begin
  if ErrorAddr = nil then writeln('before: nil') else writeln('before: FAIL, non-nil');
  ExitProc := ReportErr;
  RunError(42);
end.
