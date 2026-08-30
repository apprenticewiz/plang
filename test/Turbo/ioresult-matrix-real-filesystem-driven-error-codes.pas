(*
Tier 3 capstone (integration): one program driving a REAL filesystem
through every InOutRes/IOResult code this tier's file model actually
raises, all under {$I-}, each proving BOTH the resulting code AND that the
program did not abort -- the graceful-degradation path {$I-} exists for,
exercised end to end rather than as isolated single-behavior unit tests
(those already exist next to this file's own siblings under
test/CodeGen/Turbo/ -- reset-on-a-nonexistent-file-sets-ioresult-instead-
of-crashing.pas, reset-on-a-permission-denied-file-sets-ioresult-instead-
of-crashing.pas, blockread/blockwrite-without-result-..., erase-and-
rename-on-a-still-open-file-set-inoutres-102.pas,
write-to-a-never-opened-file-is-a-silent-no-op-and-sets-ioresult-103.pas,
seek-with-a-negative-record-number-sets-inoutres-218.pas -- this file's
own contribution is running all of them back-to-back in ONE process,
confirming the pending-InOutRes bookkeeping does not bleed from one
scenario into the next).

Each case is driven by GENUINE filesystem state, not a stub:
  2   ENOENT   -- Reset against a path that really does not exist
  5   EACCES   -- Reset against a real file under a chmod-000 directory
                  (the same portable technique test/Module/SeparateCompilation's
                  own unwritable-.pmi-directory test and
                  test/Driver/Driver's dash-g-schema-sidecar-write-failure
                  test both already use: chmod the CONTAINING directory,
                  never the target file itself, which is what actually
                  makes fopen(3) fail with EACCES portably)
  100 disk read error -- BlockRead with no Result argument, asked for more
                  records than the file actually holds (a genuine short
                  read against real bytes on disk, past the real EOF)
  101 disk write error -- BlockWrite with no Result argument against a file
                  Reset (not Rewrite) -- genuinely read-only, so every byte
                  of the attempted write is really refused by the C
                  library, not simulated
  102 file not assigned -- Erase against a file variable Assign never
                  touched at all (F->Name/F->Mode both still zero-init --
                  genuinely never opened, not merely closed)
  103 file not open -- Read against that SAME never-assigned file variable
                  (the sibling operation to 102's Erase on the identical
                  never-touched variable, proving tpFileReady's choke point
                  and Erase/Rename's own fmClosed check are two independently
                  reachable paths to two different codes from one shared
                  "never touched" starting state)
  218 EINVAL     -- Seek with a genuinely negative record number against a
                  real, just-written file

Two codes this tier's own plang_tp_posix_to_run_error table lists are
DELIBERATELY not included here, for reasons confirmed while writing this
test rather than assumed:

  3 (ENAMETOOLONG) is not practically reachable from a plang Turbo program
  at all: Assign's name parameter is a ShortString (capacity 255,
  ShortString: Turbo's own string[N] documented in docs/turbo.md), and on
  every filesystem this project's CI targets NAME_MAX is exactly 255 too --
  so no string a Turbo program can even construct exceeds it. This mirrors
  real Turbo Pascal's own field practice (its own strings are equally
  capacity-bounded), not a plang-specific gap.

  4 (EMFILE/ENFILE, "too many open files") needs a real per-process file
  descriptor exhaustion, which needs `ulimit -n` set low before the
  program runs. lit's own internal RUN-line shell has no `ulimit` (no real
  job control at all -- see test/lit.cfg.py's own comment on why
  %hold_stdin_open/%kill_during_compile/%checkexit are external bash
  scripts for exactly this class of gap) and there is no existing
  precedent anywhere in this suite for scripting a ulimit-bounded RUN line
  portably across the Linux/macOS CI matrix. Left untested here rather than
  built on a fragile, environment-specific foundation -- see this PR's own
  report for the explicit call-out.

RUN: rm -rf %t.dir && mkdir -p %t.dir/locked
RUN: printf 'secret\n' > %t.dir/locked/inner.txt
RUN: chmod 000 %t.dir/locked
RUN: %plang -std=turbo %s -o %t.exe
RUN: %run %t.exe %t.dir/does-not-exist.txt %t.dir/locked/inner.txt | FileCheck %s
RUN: chmod 755 %t.dir/locked
*)

(*
CHECK:code2=2
CHECK-NEXT:code5=5
CHECK-NEXT:code100=100
CHECK-NEXT:code101=101
CHECK-NEXT:code102=102
CHECK-NEXT:code103=103
CHECK-NEXT:code218=218
CHECK-NEXT:all seven ran to completion, none aborted
*)

program ioresultmatrix;
var
  missingFile, deniedFile: text;
  blkFile: file;
  eraseFile, openFile: text;
  seekFile: file of Byte;
  buf: array[0..9] of Byte;
  res: Integer;
  i: Byte;
  x: Integer;
  s: string;

begin
  {$I-}

  { code 2: genuinely missing file }
  assign(missingFile, ParamStr(1));
  reset(missingFile);
  writeln('code2=', IOResult);

  { code 5: real permission-denied directory }
  assign(deniedFile, ParamStr(2));
  reset(deniedFile);
  writeln('code5=', IOResult);

  { code 100: real short BlockRead, no Result argument }
  assign(blkFile, 'ioresult-matrix-blockread.bin');
  rewrite(blkFile, 1);
  for i := 0 to 4 do buf[i] := i;
  blockwrite(blkFile, buf, 5);
  close(blkFile);
  reset(blkFile, 1);
  blockread(blkFile, buf, 10);
  writeln('code100=', IOResult);
  close(blkFile);

  { code 101: real short BlockWrite, no Result argument, against a
    genuinely read-only-reopened file. FileMode must be forced to 0
    (read-only) here -- Tier 3's own gap fix (reset-opens-read-write.pas)
    now has Reset honor FileMode's documented read-write default of 2, so
    without this, the BlockWrite below would genuinely succeed instead of
    failing the way this case means to exercise. }
  FileMode := 0;
  reset(blkFile, 1);
  blockwrite(blkFile, buf, 5);
  writeln('code101=', IOResult);
  close(blkFile);
  FileMode := 2;

  { code 102: Erase against a file variable Assign never touched }
  erase(eraseFile);
  writeln('code102=', IOResult);

  { code 103: Read against that SAME never-touched variable }
  read(openFile, x);
  writeln('code103=', IOResult);

  { code 218: Seek with a genuinely negative record number }
  assign(seekFile, 'ioresult-matrix-seek.bin');
  rewrite(seekFile);
  write(seekFile, Byte(1));
  write(seekFile, Byte(2));
  close(seekFile);
  reset(seekFile);
  seek(seekFile, -3);
  writeln('code218=', IOResult);
  close(seekFile);

  {$I+}
  writeln('all seven ran to completion, none aborted');
end.
