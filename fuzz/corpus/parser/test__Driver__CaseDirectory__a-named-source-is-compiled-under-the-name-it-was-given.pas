(*
A diagnostic has to name the actual source file the compiler was given,
not some fixed internal name.

RUN: split-file %s %t.dir
RUN: not %plang -c %t.dir/mine.pas -o %t.dir/mine.o 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: mine.pas
*)

//--- mine.pas
program p; begin x := 1 end.
