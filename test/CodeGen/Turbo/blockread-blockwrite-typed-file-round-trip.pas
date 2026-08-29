(*
Tier 3 Cluster C item 6, Group B: BlockRead/BlockWrite's basic shape --
a whole typed file (`file of Integer`) written one component at a time
through the ordinary typed Write, then read back in ONE BlockRead call
using RecSize = SizeOf(Integer) (Cluster A item 4's RecSize wiring,
already confirmed empirically against `fpc -Mtp` to apply to a typed file
too, not just an untyped one -- BlockRead(TypedFile, buf, n) is legal real
Turbo Pascal). FilePos/FileSize are checked right after, in the SAME units
(records, not bytes) BlockRead itself counted in.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:0 10 20 30 40
CHECK-NEXT:FilePos=5 FileSize=5
*)

var
  f: file of Integer;
  buf: array[0..4] of Integer;
  v: Integer;
  i: Integer;
begin
  assign(f, 'blockread-blockwrite-typed-file-round-trip.bin');
  rewrite(f);
  for i := 0 to 4 do begin
    v := i * 10;
    write(f, v);
  end;
  close(f);

  reset(f);
  blockread(f, buf, 5);
  for i := 0 to 4 do write(buf[i], ' ');
  writeln;
  writeln('FilePos=', filepos(f), ' FileSize=', filesize(f));
  close(f);
end.
