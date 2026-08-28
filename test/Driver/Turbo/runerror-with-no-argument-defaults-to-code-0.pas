(*
RunError's no-argument form (Builtins.def declares it 0-or-1 arguments).
What code it defaults to is not documented by Borland in any form plang's
own project memory could find, and is not any of the numbered checks' own
codes (216, nil-deref-shaped, was one guess floated before this was
checked) -- verified empirically against `fpc -Mtp` instead: `RunError;`
with no error already pending reports "Runtime error 0" and exits 0, so
that is what plang matches here too (CGProcCall.cpp's `runerror` arm).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before
ERR: Runtime error 0 at $
*)

program runerrornoarg;
begin
  writeln('before');
  RunError;
  writeln('unreachable');
end.
