(*
The other half of BlockWrite's arity-dependent short-transfer behavior --
see blockwrite-without-result-argument-a-short-write-sets-inoutres-101.pas
right next to this test.  With a result argument, even a totally failed
write (the file is reopened read-only) is not itself an error: InOutRes
stays 0, and result silently receives the actual record count transferred
(0).  Confirmed against `fpc -Mtp` before this test was written.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:IOResult=0 result=0
*)

var
  f: file;
  buf: array[0..9] of Byte;
  res: Integer;
  i: Integer;
begin
  assign(f, 'blockwrite-with-result-argument-a-short-write-is-not-an-error.bin');
  rewrite(f, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(f, buf, 5);
  close(f);

  { FileMode forced to 0 (read-only): Tier 3's own gap fix
    (test/Turbo/reset-opens-read-write.pas) now has Reset honor FileMode's
    documented read-write default of 2, so without this, the BlockWrite
    below would genuinely succeed instead of being refused. }
  FileMode := 0;
  reset(f, 1); (* read-only *)
  blockwrite(f, buf, 5, res);
  writeln('IOResult=', IOResult, ' result=', res);
  close(f);
end.
