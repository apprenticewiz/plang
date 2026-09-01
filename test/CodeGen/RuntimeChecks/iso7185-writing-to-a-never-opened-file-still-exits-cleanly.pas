(*
Non-regression gate for abortIfClosed's (runtime/plang_file.cpp) own
BEHAVIOR: ISO 7185/Extended Pascal must keep terminating the process
unconditionally on a file operation against a file variable that was
never opened -- a program error, not a recoverable I/O condition, so it
must NOT get the -std=turbo-only tpFileReady/InOutRes treatment. Compiled
under plain -std=iso7185 (the default dialect), which reaches
plang_write_file_str -- one of the ~23 functions given a genuinely
separate `_turbo` sibling -- and confirms the ORIGINAL, untouched function
still calls abortIfClosed and still terminates the process, exactly as
before.

Issue #301 changed the MECHANISM, not this behavior: abortIfClosed used to
report this condition with std::abort() (SIGABRT, an unspecified/platform
"killed by signal" exit status, a core dump). It now reports through
plang_sys.cpp's shared plang_err_file_not_open, which follows the exact
"flush stdout, report, exit(PlangRuntimeErrorStatus)" convention every
other ISO/EP dynamic-violation in the runtime already uses (see
plang_err_div_zero and its own siblings) -- exit(70), a clean process
death with no signal and no core dump. This test used to assert `not
--crash` (i.e. specifically a SIGABRT-style death); it now asserts a
plain, specific exit(70) with %checkexit instead, which a SIGABRT (134)
would fail just as loudly as a successful exit(0) would. See the
companion test
iso7185-exiting-on-a-never-opened-file-still-flushes-prior-stdout-output.pas
for the "stdout already flushed" half of this same condition.

RUN: %plang %s -o %t
RUN: %checkexit 70 %run %t > %t.out 2> %t.err
RUN: FileCheck %s < %t.err
RUN: test ! -s %t.out
*)

(*
CHECK: file not open in 'write'
*)

program p(output);
var f: text;
begin
  writeln(f, 'this must never print')
end.
