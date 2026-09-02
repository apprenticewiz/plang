(*
issue #775: the 255 boundary, RunError's side. fpc -Mtp's own maxExitCode is
255 (rtl/unix/sysunixh.inc) and its clamp is "> maxExitCode", strictly
greater -- so 255 itself passes through UNCHANGED (ExitCode=255, $?=255,
same as always), and only 256 and up gets forced down to 255 (see
issue775-runerror-256-is-the-first-code-that-saturates's own test, its
sibling).  Both were true before this fix too for a code this small (255
fits every one of the three old independent truncations identically) -- the
point of this pair is to pin the exact edge the #775 fix's clamp now uses,
so a future regression that shifts it by one (>= instead of >, or 254/256
instead of 255) fails a test immediately.
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
ERR: Runtime error 255 at $
*)

program issue775runerror255;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  RunError(255);
  writeln('unreachable');
end.
