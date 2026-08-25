(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t 2> %t.compile.err
RUN: %run %t < %t.dir/stdin.txt > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: before it has been given
*)

//--- test.pas
program p(input, output);
type pi = ^integer;
var i: integer; q: pi;
begin read(i); new(q); q^ := i; writeln(q^); dispose(q) end.

//--- stdin.txt
7
