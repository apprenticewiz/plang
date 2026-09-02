(*
issue #775: RunError(Code)'s Code is a Turbo Integer-typed expression, but
fpc -Mtp's own RunError(w: Word) parameter is only 16 bits -- a Code past
Word's own 0..65535 range wraps mod 65536 BEFORE anything else happens, the
same way any other Word parameter would.  100000 mod 65536 = 34464, so both
the printed message and the pre-saturation ExitCode candidate are 34464, not
100000 -- and 34464 is itself well past 255, so ExitCode/$? still saturate
to 255.  Empirically confirmed against fpc -Mtp 3.2.2 (which prints "Runtime
error 34464" for RunError(100000), not "Runtime error 100000").  See
runtime/plang_sys.cpp's plang_tp_runerror for the fix.
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
ERR: Runtime error 34464 at $
*)

program issue775runerror100000;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  RunError(100000);
  writeln('unreachable');
end.
