(*
The automatic `{$I+}` check is keyed to the CHECKED STATEMENT's own source
position, not to which file (if any) that statement names -- InOutRes is
one shared global, not a per-file latch, so a plain `Writeln('...')` with
no file argument at all is just as much a checked I/O statement as a
`Write(f, ...)` naming the file that actually failed.  Real Turbo Pascal's
automatic check fires after every I/O statement (Reset/Rewrite/Read/Write/
Close/... -- CGProcCall.cpp's own dispatch chain), so a console Writeln
positioned after `{$I+}` picks up whatever InOutRes an earlier `{$I-}`-
guarded failure left pending, exactly the same as a Read or Write against
the file that actually failed would.

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
  writeln('reset returned; this console Writeln is the next checked statement');
  writeln('unreachable: the checkpoint above should already have aborted');
end.
