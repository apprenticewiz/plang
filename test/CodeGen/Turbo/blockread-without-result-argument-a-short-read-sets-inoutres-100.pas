(*
Tier 3 Cluster C item 6: BlockRead's arity-dependent short-transfer
behavior -- WITHOUT the optional 4th (result) argument, a short read (here,
asking for 20 records from a file that holds only 10) IS an error: real
Turbo Pascal / Free Pascal sets InOutRes to 100 ("disk read error").
Confirmed against `fpc -Mtp` (this item's own manual-testing requirement)
before this test was written -- not guessed at. The I-minus directive
around the call: this test is about InOutRes's own value, not about the
automatic I-plus abort a checked failure would otherwise trigger (see the
sibling io-plus-*.pas tests elsewhere in this directory for that).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:100
*)

var
  f: file;
  buf: array[0..99] of Byte;
  i: Integer;
begin
  assign(f, 'blockread-without-result-argument-a-short-read-sets-inoutres-100.bin');
  rewrite(f, 1);
  for i := 0 to 9 do buf[i] := i;
  blockwrite(f, buf, 10);
  close(f);

  reset(f, 1);
  {$I-}
  blockread(f, buf, 20);
  writeln(IOResult);
  {$I+}
  close(f);
end.
