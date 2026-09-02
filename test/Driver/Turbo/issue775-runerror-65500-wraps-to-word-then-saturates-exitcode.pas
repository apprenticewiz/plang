(*
issue #775: companion to issue775-runerror-500's own test -- 65500 exercises
the OTHER half of fpc -Mtp's RunError(w: Word) truncation: 65500 already
fits an unsigned 16-bit Word (0..65535), so it does NOT wrap the way a code
past 65535 would (see issue775-runerror-100000-wraps-past-word-range's own
test for that case), but it DOES exceed 255, so ExitCode/$? still saturate
to 255 while the printed message keeps the un-saturated, merely
Word-truncated 65500.  Before this fix, plang_tp_exitcode was
static_cast<int16_t>(65500), which wraps NEGATIVE (65500-65536=-36) --
completely different from both the printed message and the real $?.
Empirically confirmed against fpc -Mtp 3.2.2.  See runtime/plang_sys.cpp's
plang_tp_runerror for the fix.
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
ERR: Runtime error 65500 at $
*)

program issue775runerror65500;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  RunError(65500);
  writeln('unreachable');
end.
