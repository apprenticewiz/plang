(*
Issue #587: Reset(f, 0)/Rewrite(f, 0) -- an untyped file's documented
RecSize==0 special case, which sets InOutRes 2 ("file not found") without
attempting to open anything -- used to be implemented as an early return in
plang_tp_reset_sized/plang_tp_rewrite_sized that skipped past
plang_tp_reset/plang_tp_rewrite entirely, including the closeStream() call
at the top of each. If f was already open from an earlier successful
Reset/Rewrite, that old stream was left completely untouched and still
fully usable: a following BlockRead/BlockWrite against f silently kept
operating on the STALE, previous file/position with InOutRes 0, as if the
rejected Reset(f, 0) had never been attempted, instead of correctly
reporting 103 ("file not open") -- what real Turbo Pascal / `fpc -Mtp`
does, since it invalidates the file the instant Reset/Rewrite is called
whether or not that particular attempt succeeds. Both _sized wrappers now
call closeStream() on the RecSize==0 path too, before returning.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:IOResult after Reset(f,1)=0
CHECK-NEXT:IOResult after Reset(f,0)=2
CHECK-NEXT:IOResult after BlockRead following Reset(f,0)=103 n=0 b[0]=0 b[9]=0
*)

var
  f: file;
  b: array[0..9] of Byte;
  r, n: Integer;
  i: Integer;
begin
  n := 0;
  for i := 0 to 9 do b[i] := i;
  assign(f, 'reset-rewrite-recsize-0-invalidates-a-previously-open-file-inoutres-103.dat');
  rewrite(f, 1);
  blockwrite(f, b, 10);
  close(f);

  assign(f, 'reset-rewrite-recsize-0-invalidates-a-previously-open-file-inoutres-103.dat');
  {$I-}
  reset(f, 1);
  r := IOResult;
  writeln('IOResult after Reset(f,1)=', r);

  { A rejected RecSize==0 Reset must invalidate f, per real TP/FPC. }
  reset(f, 0);
  r := IOResult;
  writeln('IOResult after Reset(f,0)=', r);

  for i := 0 to 9 do b[i] := 0;
  blockread(f, b, 10, n);
  r := IOResult;
  {$I+}
  writeln('IOResult after BlockRead following Reset(f,0)=', r, ' n=', n, ' b[0]=', b[0], ' b[9]=', b[9]);
end.
