(*
plang_tp_close (runtime/plang_file.cpp) now guards its own openness (issue
#575's tpFileReady check) the same way every other Turbo entry point in this
file does, but that guard routes through setInOutResIfClear -- it never
overwrites a PENDING, unread InOutRes.  This proves the automatic check
emitted after Close is still purely POSITIONAL, not conditioned on "can this
particular call fail": a Close statement positioned after `{$I+}` is a
checked statement regardless, so it picks up whatever InOutRes an earlier
`{$I-}`-guarded Reset failure left pending (2, "file not found") rather than
Close's own would-be 103 ("file not open" -- Reset's own failure left F
closed, so Close's tpFileReady guard would report 103 here too, if InOutRes
were not already holding 2) -- the same way io-plus-console-writeln-with-no-
file-argument-still-catches-a-pending-error.pas proves for an ordinary
Writeln.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 2 %run %t %t.does-not-exist.txt 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: Runtime error 2 at $
*)

var f: text;
begin
  assign(f, ParamStr(1));
  {$I-}
  reset(f); { fails: InOutRes = 2, pending }
  {$I+}
  close(f); { cannot itself fail, but is still a checked statement }
  writeln('unreachable: Close''s own checkpoint should already have aborted');
end.
