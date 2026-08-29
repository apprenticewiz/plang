(*
This item's own core requirement: a genuine I/O error (here, Reset against
a file that does not exist) sets a sensible InOutRes rather than crashing
the process the way the ISO reset/rewrite functions (plang_err_cannot_open)
still correctly do for their own dialects.  runtime/plang_file.cpp's
plang_tp_reset, on a failed fopen, captures errno immediately and sets
InOutRes via plang_tp_posix_to_run_error(ENOENT), which maps to 2 ("file
not found") -- confirmed against the locally installed `fpc` 3.2.2's own
rtl/linux/sysos.inc table, not guessed at (see plang_tp_posix_to_run_error's
own comment).

The Readln that follows is not skipped -- it is left in on purpose, to
prove the fileReady choke point makes it a silent no-op (F stays closed, so
tpFileReady sets InOutRes to 103 and returns before touching the stream)
rather than a second crash the moment something tries to use the file Reset
never actually opened.  IOResult reads 103 there, not 2: Readln's own
tpFileReady check runs AFTER this program has already read (and so cleared)
the original 2.

`{$I-}` throughout: this file is about the fileReady CHOKE POINT (Reset
failing sets a sensible InOutRes instead of crashing), not about the
automatic `{$I+}` check a later part of this same item adds -- under that
check's real Turbo Pascal default (`{$I+}` is ON unless a program says
otherwise), the very first `reset(f)` below would itself abort the process
the moment it failed, never reaching either `writeln` this file's own CHECK
lines depend on.  See io-plus-aborts-at-the-next-checked-operation-not-
the-failing-one.pas for that check's own dedicated coverage, built
separately so this file can keep testing the choke point in isolation.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:ioresult=2
CHECK-NEXT:after-readln-ioresult=103
CHECK-NEXT:did not crash
*)

var f: text; s: string;
begin
  {$I-}
  assign(f, 'reset-on-a-nonexistent-file-this-file-does-not-exist.txt');
  reset(f);
  writeln('ioresult=', IOResult);

  readln(f, s); { f was never actually opened: silent no-op, not a crash }
  writeln('after-readln-ioresult=', IOResult);

  writeln('did not crash');
end.
