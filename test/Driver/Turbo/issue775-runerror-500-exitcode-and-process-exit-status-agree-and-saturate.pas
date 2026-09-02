(*
issue #775: RunError(Code)/Halt(Code) used to derive plang_tp_exitcode (what
an ExitProc's ExitCode reads), the printed "Runtime error N" message, and
the actual process exit status (the argument to std::exit(), later
truncated by the OS to $?) from three INDEPENDENT truncations of the same
Code -- int16_t for ExitCode, int32_t for the message/exit() argument, and
the OS's own 8-bit truncation of whatever exit() received.  For
RunError(500) that used to leave ExitCode=500, the message "Runtime error
500" (unaffected either way), and $?=244 (500's low 8 bits) -- three
different numbers.

fpc -Mtp's own RunError(w: Word) (rtl/inc/system.inc) truncates Code to an
unsigned 16-bit Word FIRST (500 already fits, so W=500, unaffected), prints
that W verbatim, and separately clamps W from ABOVE ONLY at 255 (Halt's own
maxExitCode=255 rule, rtl/unix/sysunixh.inc) for both ExitCode and the
actual exit status -- so ExitCode and $? are 255/255, even though the
printed message still legitimately says 500.  Empirically confirmed against
fpc -Mtp 3.2.2.  See runtime/plang_sys.cpp's plang_tp_runerror for the fix.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 255 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT:before
OUT-NEXT:MyExit saw ExitCode=255
ERR: Runtime error 500 at $
*)

program issue775runerror500;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  RunError(500);
  writeln('unreachable');
end.
