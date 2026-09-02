(*
Issue #573: Assign(f, newName) called while f is still physically open
(from an earlier Reset/Rewrite/Append) used to leave the OS-level stream
(F->Fp) completely untouched -- only F->Name and F->Mode were updated.
Every Turbo I/O entry point gates readiness through tpFileReady, which
checks only F->Fp != nullptr, never F->Mode, so a re-Assign left f
"logically closed" per F->Mode but writes/closes silently kept succeeding
against the OLD, now-orphaned stream -- landing data in the wrong file with
InOutRes staying 0 throughout, instead of the 103 ("file not open") real
Turbo Pascal / `fpc -Mtp` reports. plang_tp_assign now closes the stale
stream (the same closeStream() every other state-changing Turbo entry
point in this file already uses) before recording the new name, so a
following write/close correctly reports InOutRes 103, the second line
never lands anywhere, and the new name is never created on disk.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:IOResult after write-after-reassign=103
CHECK-NEXT:IOResult after close=103
CHECK-NEXT:old file line 1=first
CHECK-NEXT:old file line count=1
CHECK-NEXT:new file was created=0
*)

var
  f, g: Text;
  r: Integer;
  s: string;
  n: Integer;
begin
  {$I-}
  assign(f, 'reassign-a-still-open-file-invalidates-the-old-stream-inoutres-103-a.txt');
  rewrite(f);
  writeln(f, 'first');

  { Re-Assign f to a DIFFERENT name while f is still physically open on the
    "-a" file above. }
  assign(f, 'reassign-a-still-open-file-invalidates-the-old-stream-inoutres-103-b.txt');
  writeln(f, 'second');
  r := IOResult;
  writeln('IOResult after write-after-reassign=', r);

  close(f);
  r := IOResult;
  writeln('IOResult after close=', r);
  {$I+}

  { The "-a" file must contain only the one line written before the
    re-Assign -- 'second' must not have landed there. }
  assign(g, 'reassign-a-still-open-file-invalidates-the-old-stream-inoutres-103-a.txt');
  reset(g);
  readln(g, s);
  writeln('old file line 1=', s);
  n := 1;
  while not eof(g) do begin
    readln(g, s);
    n := n + 1;
  end;
  writeln('old file line count=', n);
  close(g);

  { The "-b" file must never have been created at all. }
  {$I-}
  assign(g, 'reassign-a-still-open-file-invalidates-the-old-stream-inoutres-103-b.txt');
  reset(g);
  r := IOResult;
  {$I+}
  writeln('new file was created=', ord(r = 0));
end.
