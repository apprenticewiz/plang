(*
issue #775: the 255 boundary, Halt's side.  Halt(255) itself is unaffected
by the #775 fix (255 fit every one of the old independent truncations
identically, and still passes through fpc -Mtp's own "> maxExitCode"
clamp -- strictly greater -- untouched); 256 is the first code the clamp
actually changes, forcing ExitCode/$? down to 255 rather than letting 256
reach the OS exit status at all (256 mod 256 is 0, the exact silent
wraparound this fix exists to avoid).  See
issue775-runerror-255-is-the-last-code-that-does-not-saturate's own test
for the same boundary on RunError's side.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 255 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:MyExit saw ExitCode=255
*)

program issue775halt256boundary;
procedure MyExit;
begin
  writeln('MyExit saw ExitCode=', ExitCode);
end;
begin
  ExitProc := MyExit;
  writeln('before');
  Halt(256);
  writeln('unreachable');
end.
