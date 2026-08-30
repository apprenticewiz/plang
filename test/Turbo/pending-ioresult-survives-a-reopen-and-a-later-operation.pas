(*
Tier 3 capstone (integration): PR #481's own finding -- once InOutRes is
left PENDING and UNREAD by a failing operation under {$I-}, a LATER
operation's own failure does not overwrite it; only an explicit IOResult
read clears it -- proven here as one realistic end-to-end scenario rather
than the isolated single-call proof
a-pending-inoutres-is-not-overwritten-by-a-later-failing-operation.pas
(test/CodeGen/Turbo/) already gives it.

The scenario a real program might actually hit: Reset a file that does not
exist (InOutRes becomes 2, pending), then -- without checking IOResult in
between -- REOPEN the same file variable against ANOTHER path that also
fails, for a DIFFERENT reason (a real permission-denied directory, which
would independently produce 5 if it were the first failure). The survives-
a-reopen wrinkle is Assign/Reset's OWN documented behavior: Assign is legal
against an already-used file variable, rebinding it to a new name, so this
is not a degenerate double-failure on the same handle but two genuinely
different Reset attempts sharing one InOutRes latch. The FIRST code (2)
is still what IOResult reports afterward, not the second (5) -- confirmed
against the same `fpc -Mtp` empirical testing
io-plus-aborts-at-the-next-checked-operation-not-the-failing-one.pas's own
comment describes. Only once IOResult is actually read does the latch
clear, ready to capture the NEXT failure fresh -- checked here too, with a
third Reset (again against the permission-denied path) now visibly
reporting 5 once nothing pending remains to protect.

RUN: rm -rf %t.dir && mkdir -p %t.dir/locked
RUN: printf 'x\n' > %t.dir/locked/inner.txt
RUN: chmod 000 %t.dir/locked
RUN: %plang -std=turbo %s -o %t.exe
RUN: %run %t.exe %t.dir/does-not-exist.txt %t.dir/locked/inner.txt | FileCheck %s
RUN: chmod 755 %t.dir/locked
*)

(*
CHECK:after first Reset (missing file): pending
CHECK-NEXT:after second Reset (denied dir, unread first error survives): 2
CHECK-NEXT:after reading IOResult once, it is clear: 0
CHECK-NEXT:after a third Reset (denied dir, no pending error left to protect): 5
*)

var f: text;
begin
  {$I-}
  assign(f, ParamStr(1));
  reset(f); { InOutRes 2, pending, unread }
  writeln('after first Reset (missing file): pending');

  assign(f, ParamStr(2)); { legal: Assign may rebind an already-used variable }
  reset(f); { would independently be InOutRes 5, but 2 is still pending and unread }
  writeln('after second Reset (denied dir, unread first error survives): ', IOResult);

  writeln('after reading IOResult once, it is clear: ', IOResult);

  reset(f); { the SAME denied-directory path, now with nothing pending }
  writeln('after a third Reset (denied dir, no pending error left to protect): ', IOResult);
end.
