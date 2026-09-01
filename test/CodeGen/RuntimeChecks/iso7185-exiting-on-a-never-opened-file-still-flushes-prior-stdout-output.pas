(*
Regression test for the "unbuffered output lost on process death" bug
(issue #301, its narrow half shipped as PR #533): abortIfClosed
(runtime/plang_file.cpp), the single choke point for all ISO/EP file-I/O
entry points, terminates the process on a file operation against a
never-opened file variable -- correct, deliberate behavior (see the
companion test
iso7185-writing-to-a-never-opened-file-still-exits-cleanly.pas, which this
test does NOT change or relitigate). Before PR #533, that termination ran
via std::abort() with no fflush(stdout) first, so any output already
written to stdout's C stream buffer -- not yet flushed to the fd because
stdout was never closed normally -- was lost outright: SIGABRT, empty
stdout, core dump. PR #533 added an explicit fflush(stdout) at this call
site to fix that, without changing the abort() itself.

Issue #301's broader half (this test's own update) then replaced that
abort() with plang_sys.cpp's shared plang_err_file_not_open -- fflush
stdout, report, exit(70) -- so the explicit fflush(stdout) PR #533 added
is now ALSO covered by exit()'s own guarantee to flush every open C
stream, not just stdout, before the process ends. The explicit fflush
stays (see plang_err_file_not_open's own comment for why: every other
runtime error reporter in this file already does the identical explicit
fflush before its own exit(70), and plang_halt's own comment, just above
in this same file, already documents this project's deliberate choice not
to rely solely on an implementation's automatic stream cleanup at exit) --
so this test still exercises a real code path, not a dead one, and still
guards against a future edit that reorders the fflush after the report or
drops it.

This program writes to stdout FIRST (unlike the companion test's program,
whose writeln(f, ...) never touches stdout at all), then hits the same
abortIfClosed condition. With PR #533's fix (and now, still, after issue
#301's broader change), the already-buffered "before" line survives and
appears on stdout despite the process dying with exit(70) -- no longer by
SIGABRT.

RUN: %plang %s -o %t
RUN: %checkexit 70 %run %t > %t.out 2> %t.err
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK: before the abort
*)
(*
ERR: file not open in 'write'
*)

program p(output);
var f: text;
begin
  writeln('before the abort');
  writeln(f, 'this must never print')
end.
