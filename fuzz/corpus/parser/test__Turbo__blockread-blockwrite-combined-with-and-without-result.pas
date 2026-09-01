(*
Tier 3 capstone (integration): a realistic combined BlockRead/BlockWrite
scenario -- write N typed records with BlockWrite, then attempt to read
MORE than N back, once WITH a Result argument (silent, correct transfer
count, InOutRes stays 0) and once WITHOUT one (aborts the checked read
under the default {$I+}, exit 100) -- going past the isolated single-arity
unit tests (blockread-with/without-result-argument-*.pas,
test/CodeGen/Turbo/) to the actual reason a real Turbo program picks one
arity over the other: WITH Result, a program can keep going and act on
however many records it actually got; WITHOUT it, a short read is meant
to be fatal.

Two RUN-line pairs sharing one lit file, split-file'd apart: the WITH-
Result half prints the true count and keeps running; the WITHOUT-Result
half is a genuinely separate compile (Turbo's checked-statement position
is textual, so the two arities cannot coexist as "the same statement" in
one program) that aborts.

RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/with-result.pas -o %t.with
RUN: %run %t.with %t.with.dat | FileCheck --check-prefix=WITH %s

RUN: %plang -std=turbo %t.dir/without-result.pas -o %t.without
RUN: %checkexit 100 %run %t.without %t.without.dat 2> %t.without.err
RUN: FileCheck --check-prefix=WITHOUT %s < %t.without.err
*)

(*
WITH: wrote 6 records
WITH-NEXT: asked for 20, got 6
WITH-NEXT: ioresult=0
WITH-NEXT: first record=10
WITH-NEXT: last record read=60

WITHOUT: Runtime error 100 at $
*)

//--- with-result.pas
var
  f: file;
  outBuf, inBuf: array[0..19] of Integer;
  i: Integer;
  got: Integer;
begin
  assign(f, ParamStr(1));
  rewrite(f, SizeOf(Integer));
  for i := 0 to 5 do outBuf[i] := (i + 1) * 10;
  blockwrite(f, outBuf, 6);
  writeln('wrote 6 records');
  close(f);

  reset(f, SizeOf(Integer));
  blockread(f, inBuf, 20, got);
  writeln('asked for 20, got ', got);
  writeln('ioresult=', IOResult);
  writeln('first record=', inBuf[0]);
  writeln('last record read=', inBuf[got - 1]);
  close(f);
end.

//--- without-result.pas
var
  f: file;
  outBuf, inBuf: array[0..19] of Integer;
  i: Integer;
begin
  assign(f, ParamStr(1));
  rewrite(f, SizeOf(Integer));
  for i := 0 to 5 do outBuf[i] := (i + 1) * 10;
  blockwrite(f, outBuf, 6);
  close(f);

  reset(f, SizeOf(Integer));
  blockread(f, inBuf, 20); { no result argument: checked, and it's short -> aborts }
  writeln('unreachable');
  close(f);
end.
