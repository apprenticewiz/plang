(*
HeapError returning anything other than 1 (0, "not handled", is real
Borland's own value for this) reports Runtime error 203 -- the numbered
"out of memory" error -- through the same plang_tp_runerror mechanism every
other numbered runtime check already uses, rather than leaving GetMem to
return nil silently.  See heaperror-returning-1-makes-getmem-return-nil-
and-runs-the-handler.pas for the opposite (handled) case.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 203 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: handler declined
ERR: Runtime error 203 at $
*)

program heaperrorreturns0;
var
  p: Pointer;

function MyHeapError(Size: Int64): Int64;
begin
  writeln('handler declined');
  MyHeapError := 0;
end;

begin
  HeapError := MyHeapError;
  GetMem(p, 9000000000000000000);
  writeln('unreachable');
end.
