(*
Borland/FPC's "Runtime error 216: General protection fault" -- what a real
Turbo/FPC program gets from the OS trapping a bad pointer access, nil
dereference included (confirmed against `fpc -Mtp`: `p := nil;
writeln(p^);` reports exactly this).  plang checks explicitly rather than
relying on a trap (RangeCheckGuards::emitNilCheck), but under -std=turbo
now reports the same number through the new plang_tp_runerror(216)
reporter instead of the shared ISO/EP plang_err_nil_deref (exit
PlangRuntimeErrorStatus, 70).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 216 %run %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 216 at $
CHECK-NOT: plang runtime:
*)

program nilderef;
var
  p: ^Integer;
begin
  p := nil;
  writeln('unreachable: ', p^);
end.
