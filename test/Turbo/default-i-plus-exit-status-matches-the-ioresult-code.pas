(*
Tier 3 capstone (integration): the flip side of
ioresult-matrix-real-filesystem-driven-error-codes.pas's own {$I-} matrix
-- under Turbo's REAL default ({$I+} unless a program says otherwise),
each of those same genuine failures aborts the process immediately at the
next checked I/O statement, with the process exit STATUS equal to the
IOResult code itself and stderr matching "Runtime error <n> at $<addr>",
never the shared ISO/EP plang_err_* wording or exit status. Two of the
matrix's own codes are pinned here end to end, each its own compile +
run (a single program cannot demonstrate two different fatal aborts, so
this is two RUN-line pairs sharing one lit file rather than two separate
files, matching this project's own "one file when the two halves are
directly the same proof" style):

  2   Reset against a genuinely missing file
  218 Seek with a genuinely negative record number against a real file

Both already have {$I-}-guarded, non-aborting siblings
(reset-on-a-nonexistent-file-sets-ioresult-instead-of-crashing.pas,
seek-with-a-negative-record-number-sets-inoutres-218.pas, both
test/CodeGen/Turbo/) and a THIRD sibling proving which position's checked
statement is the one that actually fires
(io-plus-aborts-at-the-next-checked-operation-not-the-failing-one.pas);
this file is the direct default-{$I+}-abort proof for the SAME two codes,
completing the pair the matrix's own {$I-} side established.

RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/missing-file.pas -o %t.missing
RUN: %checkexit 2 %run %t.missing %t.missing.does-not-exist.txt 2> %t.missing.err
RUN: FileCheck --check-prefix=RTE2 %s < %t.missing.err

RUN: %plang -std=turbo %t.dir/negative-seek.pas -o %t.seek
RUN: %checkexit 218 %run %t.seek 2> %t.seek.err
RUN: FileCheck --check-prefix=RTE218 %s < %t.seek.err
*)

(*
RTE2: Runtime error 2 at $
RTE2-NOT: plang runtime:

RTE218: Runtime error 218 at $
RTE218-NOT: plang runtime:
*)

//--- missing-file.pas
var f: text;
begin
  assign(f, ParamStr(1));
  reset(f); { no I-minus anywhere: this is a checked statement, and it fails }
  writeln('unreachable');
end.

//--- negative-seek.pas
var
  f: file of Byte;
begin
  assign(f, 'default-i-plus-negative-seek.bin');
  rewrite(f);
  write(f, Byte(1));
  close(f);
  reset(f);
  seek(f, -7); { no I-minus anywhere: this is a checked statement, and it fails }
  writeln('unreachable');
end.
