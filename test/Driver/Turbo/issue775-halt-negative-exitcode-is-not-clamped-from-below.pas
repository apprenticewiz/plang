(*
issue #775: the #775 fix's clamp is deliberately ONE-SIDED, matching fpc
-Mtp's own Halt(ErrNum: Longint) exactly (rtl/inc/system.inc): `if ErrNum >
maxExitCode then ExitCode := 255 else ExitCode := ErrNum` only ever forces
ExitCode UP to 255 when it is too big, never up to 0 when it is negative.
Halt(-1)'s ExitCode is genuinely -1 on real fpc -Mtp (empirically
confirmed) -- an ExitProc that reads ExitCode after a negative Halt sees
that same negative value, not 0 or 255 -- while the process's own $? is
still 255, because passing a negative int to the OS's own exit() truncates
to its low 8 bits exactly the way any other out-of-byte-range value would
(-1's two's-complement low byte is all-ones).  This test exists so a future
"fix" that clamps from BOTH sides (as the issue's own initial "saturating"
description could be misread to imply) fails immediately: ExitCode must
stay -1 here, not become 0.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 255 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:MyExit saw ExitCode=-1
*)

program issue775haltnegative;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  Halt(-1);
  writeln('unreachable');
end.
