(*
The other half of BlockWrite's arity-dependent short-transfer behavior --
see
blockwrite-without-result-argument-a-direction-violation-sets-inoutres-105.pas
right next to this test.

Issue #665 correction: this test used to claim that, WITH a result
argument, even a totally failed write (the file is reopened read-only) is
not itself an error at all -- InOutRes staying 0, with result silently
receiving the actual record count transferred (0), and no checked-I/O-off
directive needed to observe that. Re-confirmed against `fpc -Mtp` while
fixing #665: that is wrong -- under the default checked-I/O-on setting,
this exact setup actually TRAPS with "Runtime error 105" the instant
BlockWrite itself runs, never reaching a following Writeln at all. A
result argument only ever suppresses the "transfer came up short, but
otherwise fine" case (the one BlockRead's own natural-end-of-file short
read genuinely is); it was never meant to -- and in real Borland/FPC field
practice does not -- suppress a genuine hard error like this direction
violation. Checked I/O is now turned off (see the source below) here
specifically so this test CAN observe both IOResult and result together
instead of just trapping: IOResult still reports the real 105, exactly as
it does without a result argument, while result still receives the actual
count (0) -- the one place arity actually matters is that turning it off
is needed to see either value at all, not whether the error itself gets
reported.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:IOResult=105 result=0
*)

var
  f: file;
  buf: array[0..9] of Byte;
  res: Integer;
  i: Integer;
begin
  assign(f, 'blockwrite-with-result-argument-a-direction-violation-is-still-reported.bin');
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
  {$I-}
  blockwrite(f, buf, 5, res);
  writeln('IOResult=', IOResult, ' result=', res);
  {$I+}
  close(f);
end.
