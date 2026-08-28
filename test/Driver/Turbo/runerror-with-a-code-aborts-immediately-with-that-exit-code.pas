(*
TP RunError(errorcode) (Builtins.def, CGProcCall.cpp) -- aborts
immediately, the same way one of the numbered checks would, printing
"Runtime error <n> at $<addr>" through the shared plang_tp_runerror
reporter and exiting with status n itself.  199 rather than one of the
numbers a real check already uses (200/201/215/216): RunError takes any
Integer, not just Borland's own reserved codes, and this keeps the test
from being confusable with one of THOSE checks accidentally firing instead
of RunError actually running.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 199 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before
ERR: Runtime error 199 at $
*)

program runerrorwithacode;
begin
  writeln('before');
  RunError(199);
  writeln('unreachable');
end.
