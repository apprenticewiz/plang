(*
issue #775: Halt's own half of the same three-way-truncation bug.
Halt(300) used to leave plang_tp_exitcode=300 (an ExitProc's ExitCode saw
300), but the real process exit status truncated 300 to its own low 8 bits
at the OS level ($?=44) -- two different numbers an ExitProc and the
program's caller would each observe for the "same" exit.

fpc -Mtp's own Halt(ErrNum: Longint) (rtl/inc/system.inc) clamps ErrNum from
ABOVE ONLY at maxExitCode=255 (rtl/unix/sysunixh.inc) for ExitCode itself,
so Halt(300)'s ExitCode is really 255, and 255 is also what reaches the
OS-level exit(), so $?=255 too -- the two agree, unlike the old
independently-truncated pair.  Empirically confirmed against fpc -Mtp
3.2.2.  See runtime/plang_sys.cpp's plang_halt for the fix.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 255 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:MyExit saw ExitCode=255
*)

program issue775halt300;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  Halt(300);
  writeln('unreachable');
end.
