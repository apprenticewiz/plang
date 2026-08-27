(*
Issue #275: issue #125/#134 taught the driver's own entry point that a
directory given where an input file is expected must be refused with one
clear diagnostic instead of read as source -- but "plang -pc1" is a second,
separate entry point (the front end's own, reached directly here rather
than through the driver's checks at all), and it never got the same guard.
A directory opens fine as an ifstream on Linux and reads back empty, so the
front end went on to scan zero bytes and reported a cascade of unrelated
"expected X, got end of file" parser errors instead of naming the real
problem.
*)

(*
RUN: mkdir -p %t.dir/fakedir.pas
RUN: not %plang_ir -pc1 %t.dir/fakedir.pas > %t.out 2>&1
RUN: FileCheck %s < %t.out
RUN: FileCheck --check-prefix=ABSENT %s < %t.out
*)

(*
CHECK: error: is a directory, not a file
ABSENT-NOT: expected 'program', got end of file
*)
