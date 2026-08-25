(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:AB
*)

//--- test.pas
program p(input, output);
var a, b: char;
begin read(input, a); read(b); writeln(a, b) end.

//--- stdin.txt
AB
