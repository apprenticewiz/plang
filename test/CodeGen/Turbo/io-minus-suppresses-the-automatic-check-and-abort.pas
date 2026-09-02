(*
The other half of io-plus-aborts-at-the-next-checked-operation-not-the-
failing-one.pas's own proof: a failing I/O statement written entirely under
`{$I-}` -- with NO later checked statement to catch the pending error --
must never abort the process at all.  InOutRes still ends up set (the
fileReady choke point's own job, PR #480/#478), readable through an
explicit IOResult call, but RangeCheckGuards::ioChecksAt(s.Loc) is false
for every statement here, so CGProcCall.cpp never emits the
`call void @plang_iocheck()` that would check it automatically.

Issue #738 update: `reset(f)`'s own InOutRes 2 is left pending and unread
right into the very next statement -- confirmed against `fpc -Mtp`, the
`writeln('reset-failed-but-did-not-abort')` that used to print unconditionally
is now ITSELF suppressed outright (nothing prints, not even a blank line),
and the following `writeln('ioresult=', IOResult)` loses its own leading
literal for the same "IOResult is the first write attempt in the statement
to actually clear InOutRes" reason every other test in this item's fix
does; only the numeric value prints.  The final `program-reached-the-end`
writeln runs with InOutRes already back at 0 and is unaffected -- still
proving the process never aborted.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t %t.does-not-exist.txt | FileCheck %s
*)

(*
CHECK:2
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
