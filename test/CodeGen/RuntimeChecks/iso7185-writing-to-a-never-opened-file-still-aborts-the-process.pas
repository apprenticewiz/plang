(*
Non-regression gate for this item's own "abortIfClosed itself is UNCHANGED"
rule (runtime/plang_file.cpp): ISO 7185/Extended Pascal must keep aborting
the process unconditionally on a file operation against a file variable
that was never opened -- a program error, not a recoverable I/O condition,
so it must NOT get the -std=turbo-only tpFileReady/InOutRes treatment this
item adds.  Compiled under plain -std=iso7185 (the default dialect), which
reaches plang_write_file_str -- one of the ~23 functions this item gave a
genuinely separate `_turbo` sibling to -- and confirms the ORIGINAL,
untouched function still calls abortIfClosed and still aborts, exactly as
before this item existed.

RUN: %plang %s -o %t
RUN: not --crash %run %t > %t.out 2> %t.err
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
