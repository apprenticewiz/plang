(*
HeapError (Sema::registerBuiltins, -std=turbo only) is a settable
procedural VALUE (Tier 2's procedural types/values, reused here rather than
a second function-pointer mechanism) that plang_tp_getmem
(runtime/plang_sys.cpp) calls through on an allocation failure.  Returning 1
from it is real Borland Turbo Pascal's own documented "handled -- give me
nil instead" signal; confirm the handler actually runs (its own side effect
is observable) and that GetMem really does answer nil once it does.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 0 %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:handler saw size=9000000000000000000
CHECK-NEXT:nil as expected
CHECK-NEXT:calls=1
*)

program heaperrorreturns1;
var
  p: Pointer;
  calls: Integer;

function MyHeapError(Size: Int64): Int64;
begin
  calls := calls + 1;
  writeln('handler saw size=', Size);
  MyHeapError := 1;
end;

begin
  calls := 0;
  HeapError := MyHeapError;
  GetMem(p, 9000000000000000000);
  if p = nil then writeln('nil as expected') else writeln('FAIL: non-nil');
  writeln('calls=', calls);
end.
