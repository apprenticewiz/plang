(*
The other half of io-plus-aborts-at-the-next-checked-operation-not-the-
failing-one.pas's own proof: a failing I/O statement written entirely under
`{$I-}` -- with NO later checked statement to catch the pending error --
must never abort the process at all.  InOutRes still ends up set (the
fileReady choke point's own job, PR #480/#478), readable through an
explicit IOResult call, but RangeCheckGuards::ioChecksAt(s.Loc) is false
for every statement here, so CGProcCall.cpp never emits the
`call void @plang_iocheck()` that would check it automatically.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t %t.does-not-exist.txt | FileCheck %s
*)

(*
CHECK:reset-failed-but-did-not-abort
CHECK-NEXT:ioresult=2
CHECK-NEXT:program-reached-the-end
*)

var f: text;
begin
  assign(f, ParamStr(1));
  {$I-}
  reset(f);
  writeln('reset-failed-but-did-not-abort');
  writeln('ioresult=', IOResult);
  writeln('program-reached-the-end');
end.
