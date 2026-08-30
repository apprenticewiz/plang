(*
BlockWrite's own arity-dependent short-transfer behavior -- the write-side
twin of blockread-without-result-argument-a-short-read-sets-inoutres-100.pas,
right next to this test.  Without a result argument, a short (here: totally
failed -- the file is reopened read-only, so every byte of the attempted
write is refused at the C stdio level) write IS an error: InOutRes 101
("disk write error"), not 100 -- confirmed against `fpc -Mtp` before this
test was written, the identical setup (Reset a normally-writable file with
no name change, but opened for READING) this project's own manual testing
used to exercise it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:101
*)

var
  f: file;
  buf: array[0..9] of Byte;
  i: Integer;
begin
  assign(f, 'blockwrite-without-result-argument-a-short-write-sets-inoutres-101.bin');
  rewrite(f, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(f, buf, 5);
  close(f);

  reset(f, 1); (* read-only: every byte of a following write is refused *)
  {$I-}
  blockwrite(f, buf, 5);
  writeln(IOResult);
  {$I+}
  close(f);
end.
