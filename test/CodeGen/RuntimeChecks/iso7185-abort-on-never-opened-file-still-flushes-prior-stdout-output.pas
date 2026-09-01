(*
Regression test for the "unbuffered output lost on abort" bug (issue #301):
abortIfClosed (runtime/plang_file.cpp), the single choke point for all
ISO/EP file-I/O entry points, calls std::abort() on a file operation
against a never-opened file variable -- correct, deliberate behavior (see
the companion test
iso7185-writing-to-a-never-opened-file-still-aborts-the-process.pas, which
this test does NOT change or relitigate).  But before this item, that
abort() ran with no fflush(stdout) first, so any output already written to
stdout's C stream buffer -- not yet flushed to the fd because stdout was
never closed normally -- was lost outright: SIGABRT, empty stdout, core
dump.

This program writes to stdout FIRST (unlike the companion test's program,
whose writeln(f, ...) never touches stdout at all), then hits the same
abortIfClosed condition.  With the fix, the already-buffered "before" line
survives the abort and appears on stdout despite the process dying by
SIGABRT.

RUN: %plang %s -o %t
RUN: not --crash %run %t > %t.out 2> %t.err
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
