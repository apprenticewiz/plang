(*
issue #775: the 255 boundary, RunError's side -- the other half of
issue775-runerror-255-is-the-last-code-that-does-not-saturate's own test.
256 is the first code fpc -Mtp's clamp actually changes: ExitCode/$? both
become 255 rather than staying 256 (256 itself does not fit an OS exit
status at all -- 256 mod 256 is 0, which is exactly the kind of silent
wraparound the #775 fix's saturating clamp exists to avoid). The printed
message still says 256 (Word(256)=256, no wrap needed, this file's own
comment on why the message and ExitCode/$? are allowed to differ has the
full reasoning -- see issue775-runerror-500's own test).
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
ERR: Runtime error 256 at $
*)

program issue775runerror256;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  RunError(256);
  writeln('unreachable');
end.
