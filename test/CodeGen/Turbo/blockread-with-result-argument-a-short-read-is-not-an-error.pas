(*
The other half of BlockRead's arity-dependent short-transfer behavior --
see blockread-without-result-argument-a-short-read-sets-inoutres-100.pas
right next to this test for the WITHOUT-result case.  WITH the optional
4th (result) argument, a short read is NOT an error: InOutRes stays 0, and
result silently receives the actual record count transferred (10, not the
20 asked for).  Confirmed against `fpc -Mtp` before this test was written.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:IOResult=0 result=10
*)

var
  f: file;
  buf: array[0..99] of Byte;
  res: Integer;
  i: Integer;
begin
  assign(f, 'blockread-with-result-argument-a-short-read-is-not-an-error.bin');
  rewrite(f, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(f, buf, 10);
  close(f);

  reset(f, 1);
  blockread(f, buf, 20, res);
  writeln('IOResult=', IOResult, ' result=', res);
  close(f);
end.
