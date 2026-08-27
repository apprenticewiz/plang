(*
Issue #125: a directory given where an input file is expected used to be
opened and read as source anyway -- which succeeds at open() but reads
back empty, so the front end reported six unrelated "expected X, got end
of file" parser errors instead of naming the real problem.  One condition
should give one diagnostic.
*)

(*
RUN: mkdir -p %t.dir/fakedir.pas
RUN: not %plang_ir %t.dir/fakedir.pas > %t.out 2>&1
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ABSENT %s < %t.out
*)

(*
CHECK: is a directory, not a file
ABSENT-NOT: expected 'program', got end of file
*)
