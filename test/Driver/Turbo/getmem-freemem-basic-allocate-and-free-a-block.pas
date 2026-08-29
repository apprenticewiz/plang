(*
TP GetMem(var P: Pointer; Size: Int64) / FreeMem(P: Pointer[, Size: Int64])
(Builtins.def, CGProcCall.cpp) -- the ordinary success path, routed through
runtime/plang_sys.cpp's plang_tp_getmem/plang_tp_freemem: allocate a block,
confirm it is non-nil and usable through a typed pointer (not just the
generic Pointer), then free it.  See the other getmem-*/freemem-*/
heaperror-*.pas files in this directory for the non-aborting failure path
and HeapError.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:non-nil
CHECK-NEXT:value=42
CHECK-NEXT:freed
*)

program getmembasic;
type
  PInt = ^Integer;
var
  p: PInt;
begin
  GetMem(p, SizeOf(Integer));
  if p <> nil then writeln('non-nil') else writeln('nil');
  p^ := 42;
  writeln('value=', p^);
  FreeMem(p, SizeOf(Integer));
  writeln('freed');
end.
