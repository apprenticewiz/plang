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
  105 file not open for output -- BlockWrite with no Result argument
                  against a file Reset (not Rewrite) -- genuinely
                  read-only, so every byte of the attempted write is
                  really refused by the C library, not simulated. (Issue
                  #665 correction: this case used to be labeled/checked as
                  101 here, on the same now-known-wrong "a short write
                  without a Result argument always means 101" assumption
                  blockwrite-without-result-argument-a-direction-violation-
                  sets-inoutres-105.pas, test/CodeGen/Turbo/, corrects on
                  its own -- see that test's own comment. A genuinely-101
                  case, driven by a real ENOSPC rather than a direction
                  violation, is covered separately by
                  write-to-a-disk-full-device-reports-the-real-ioresult-
                  not-105.pas and
                  blockwrite-to-a-disk-full-device-reports-101-even-with-a-
                  result-argument.pas, both test/CodeGen/Turbo/ -- not
                  folded into this capstone, since both need the "dev-full"
                  lit feature gate this file's other six codes do not.)
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

Issue #738 update: every `writeln('codeN=', IOResult)` below starts with
InOutRes already pending -- each case's own failing operation, right above
it, is exactly what that writeln means to report -- so, confirmed against
`fpc -Mtp`, every one of the seven loses its own leading literal
('codeN='), since IOResult is the first write attempt in each statement to
actually clear InOutRes; only the bare numeric value prints each time.
This chains cleanly precisely because it is what this file's own comment
above already promises: each writeln's OWN IOResult call clears InOutRes
before the NEXT case's failing operation runs, so no code here leaks into
the next the way a genuinely unread, still-pending one would.  The closing
'all seven ran to completion' writeln runs with InOutRes already back at 0
and is unaffected.

RUN: rm -rf %t.dir && mkdir -p %t.dir/locked
RUN: printf 'secret\n' > %t.dir/locked/inner.txt
RUN: chmod 000 %t.dir/locked
RUN: %plang -std=turbo %s -o %t.exe
RUN: %run %t.exe %t.dir/does-not-exist.txt %t.dir/locked/inner.txt | FileCheck %s
RUN: chmod 755 %t.dir/locked
*)

(*
CHECK:2
CHECK-NEXT:5
CHECK-NEXT:100
CHECK-NEXT:105
CHECK-NEXT:102
CHECK-NEXT:103
CHECK-NEXT:218
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

  { code 105: real short BlockWrite, no Result argument, against a
    genuinely read-only-reopened file -- a direction violation, not a
    disk-write error (issue #665 correction: see this file's own top
    comment for why this is 105, not the 101 this case used to claim).
    FileMode must be forced to 0 (read-only) here -- Tier 3's own gap fix
    (reset-opens-read-write.pas) now has Reset honor FileMode's documented
    read-write default of 2, so without this, the BlockWrite below would
    genuinely succeed instead of failing the way this case means to
    exercise. }
  FileMode := 0;
  reset(blkFile, 1);
  blockwrite(blkFile, buf, 5);
  writeln('code105=', IOResult);
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
