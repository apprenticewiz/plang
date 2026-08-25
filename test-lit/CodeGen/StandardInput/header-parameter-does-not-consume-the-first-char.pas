(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A
*)

//--- test.pas
program p(input, output);
var c: char;
begin read(c); writeln(c) end.

//--- stdin.txt
AB
