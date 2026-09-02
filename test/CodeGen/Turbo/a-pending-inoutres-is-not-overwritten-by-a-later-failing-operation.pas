(*
Isolates the runtime-level half of io-plus-aborts-at-the-next-checked-
operation-not-the-failing-one.pas's own proof from the automatic-check
CodeGen path entirely, by reading IOResult explicitly instead of letting a
checked statement's abort report the surviving code: once Reset leaves
InOutRes PENDING and UNREAD (2, "file not found"), a later Read against the
same still-closed file must not overwrite it with its own "file not open"
code (103) -- confirmed against the local `fpc -Mtp` install (see
runtime/plang_file.cpp's setInOutResIfClear for the full empirical
write-up).  Everything here runs under `{$I-}`, so this is purely about
runtime/plang_file.cpp's own InOutRes bookkeeping, not about
RangeCheckGuards::ioChecksAt or plang_iocheck at all.

Issue #738 update: `read(f, x)` right below is now ITSELF suppressed
outright while the pending 2 sits unread (confirmed against `fpc -Mtp`: it
never attempts tpFileReady's own would-be 103 at all) -- a stronger, but
consistent, way of reaching the same "2 survives, not 103" conclusion this
test was already proving. The closing `writeln` is ordinary Turbo I/O too:
its own literal ('ioresult=') is suppressed right along with everything
else, since IOResult -- evaluated as this statement's SECOND argument -- is
the first write attempt in it to actually clear InOutRes; only the numeric
value that call returns still prints.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t %t.does-not-exist.txt | FileCheck %s
*)

(*
CHECK:2
*)

var f: text; x: integer;
begin
  assign(f, ParamStr(1));
  {$I-}
  reset(f);  { fails: InOutRes = 2, pending }
  read(f, x); { file still closed: must NOT overwrite the pending 2 with 103 }
  writeln('ioresult=', IOResult);
end.
