(*
Issue #679: Reset(f, RecSize) only special-cases RecSize 0 (InOutRes 2,
"file not found", without opening anything -- plang_tp_reset_sized's own
comment, runtime/plang_file.cpp); a NEGATIVE RecSize was passed straight
through unchecked, so Reset(f, -1) opened the file successfully (InOutRes 0,
matching real Turbo Pascal/`fpc -Mtp`) but BlockRead/BlockWrite against it
used to treat RecSize <= 0 as "transfer zero records, never an error" --
silently returning 0 rather than trapping.  Confirmed against a local
`fpc -Mtp` 3.2.2 install: BlockRead against a file Reset with a negative
RecSize aborts with "Runtime error 217", not a quiet no-op -- while
BlockRead with Count 0 against that SAME file does not trap at all (there is
nothing to transfer either way), which this also checks does not regress.

RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 217 %run %t %t.dat > %t.out 2> %t.err
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK:ioresult after reset=0
CHECK-NEXT:count=0 blockread did not trap, n=0
ERR: Runtime error 217 at $
*)

var
  f: file;
  buf: array[0..15] of Byte;
  n: Integer;
begin
  {$I-}
  assign(f, ParamStr(1));
  rewrite(f);
  close(f);

  assign(f, ParamStr(1));
  reset(f, -1);
  writeln('ioresult after reset=', IOResult);

  blockread(f, buf, 0, n);
  writeln('count=0 blockread did not trap, n=', n);

  blockread(f, buf, 1, n); { must trap: Runtime error 217 }
  writeln('unreachable: blockread with a negative RecSize should have trapped');
end.
