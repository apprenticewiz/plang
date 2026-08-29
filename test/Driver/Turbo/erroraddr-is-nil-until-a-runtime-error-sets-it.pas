(*
ErrorAddr (Sema::registerBuiltins, -std=turbo only) -- deliberately
simplified (runtime/plang_sys.cpp's own plang_tp_erroraddr comment): nil
(0) until the FIRST genuine runtime fault this project actually tracks it
at (RunError, or Halt with a nonzero status), not wired to every individual
runtime-error call site.  Confirms the "nil until then" half; see
erroraddr-is-observable-from-inside-a-runtime-errors-own-exitproc-
handler.pas for the "set once one occurs" half.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 0 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:nil as expected
*)

program erroraddrnil;
begin
  if ErrorAddr = nil then writeln('nil as expected') else writeln('FAIL: non-nil');
end.
