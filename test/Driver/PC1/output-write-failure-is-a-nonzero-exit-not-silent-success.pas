(*
Issue #246: a write failure on the compiler's OWN requested output (an -o
target, not a compiled program's runtime I/O) was reported as success.

/dev/full always opens successfully but fails every write with ENOSPC, so
"plang ... -o /dev/full" reached Frontend.cpp's withOutput lambda and its
raw Cg.emit call with an already-open, already-good std::ofstream -- the
open-failure check both sites already had never fired, and neither site
checked the stream's error state afterward, so a write that failed after
the open was never noticed: both exited 0 with nothing on stderr.

Covers both gaps the issue named: withOutput (the -dump-ast path here) and
the raw Cg.emit call (the default -pc1 IR path and, through it, the
driver's -emit-llvm, which re-execs "plang -pc1 ... -o ..." as a real
subprocess and forwards its exit code as its own).  A normal write to a
real path is checked too, so the fix cannot be a blanket "always fail".
*)

(*
RUN: not %plang_ir -pc1 %s -o /dev/full 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err

RUN: not %plang_ir -pc1 -dump-ast %s -o /dev/full 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err

RUN: not %plang -emit-llvm %s -o /dev/full 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err

RUN: %plang_ir -pc1 %s -o %t.ll
RUN: FileCheck --check-prefix=IR %s < %t.ll
*)

(*
REQUIRES: dev-full
*)

(*
ERR: error writing output file
*)

(*
IR: ModuleID
*)

program ok;
begin
  writeln('hi');
end.
