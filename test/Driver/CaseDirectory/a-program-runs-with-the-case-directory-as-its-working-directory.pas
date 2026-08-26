(*
A relative name in the source has to mean a file in this case's own
directory. Before this, the program ran wherever ctest happened to be, so
a case that wrote 'r.txt' wrote it into the build tree, left it there,
and would have read the previous run's copy had it not been overwritten
first. lit's own %t.dir isolation gives this property for free, but the
underlying product behavior -- the compiled program's own relative-path
I/O being resolved against ITS OWN working directory, not wherever it
happened to be invoked from -- is still worth a real end-to-end check.

RUN: split-file %s %t.dir
RUN: %plang %t.dir/case.pas -o %t.dir/case.out
RUN: cd %t.dir && %run %t.dir/case.out
RUN: FileCheck %s < %t.dir/made.txt
*)

(*
CHECK:written
*)

//--- case.pas
program p(output);
var f: text;
begin rewrite(f, 'made.txt'); writeln(f, 'written'); close(f) end.
