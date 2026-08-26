(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:xyz
*)

//--- test.pas
program p(input, output);
var a, b, c: char;
begin read(a); read(b); read(c); writeln(a, b, c) end.

//--- stdin.txt
xyz
