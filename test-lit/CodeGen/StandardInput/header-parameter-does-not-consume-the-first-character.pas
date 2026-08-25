(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

//--- test.pas
program p(input, output);
var i: integer;
begin read(i); writeln(i) end.

//--- stdin.txt
42
