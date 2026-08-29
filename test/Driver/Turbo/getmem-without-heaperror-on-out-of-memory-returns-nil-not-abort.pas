(*
plang_tp_getmem's own deliberate divergence from real Borland Turbo Pascal
7 (runtime/plang_sys.cpp's own comment on plang_tp_getmem): with no
HeapError installed, a failing GetMem returns nil and the process keeps
running, rather than halting with Runtime error 203 the way real Borland's
own default does.  This is GetMem/FreeMem's whole reason for being a
separate, non-aborting pair of runtime entry points from New/Dispose's
plang_new/plang_dispose, which DO abort unconditionally.  The size asked
for (9 quintillion bytes) can never succeed on any real system, so this is a
deterministic, portable way to simulate out-of-memory without relying on
ulimit or any other environment-specific constraint.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 0 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:nil as expected
CHECK-NEXT:still running
*)

program getmemnoheaperror;
var
  p: Pointer;
begin
  GetMem(p, 9000000000000000000);
  if p = nil then writeln('nil as expected') else writeln('FAIL: non-nil');
  writeln('still running');
end.
