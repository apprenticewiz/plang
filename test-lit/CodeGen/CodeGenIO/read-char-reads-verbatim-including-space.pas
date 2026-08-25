(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:65 32 66
*)

//--- test.pas
program p;
var a, b, c: char;
begin read(a); read(b); read(c);
  writeln(ord(a), ' ', ord(b), ' ', ord(c)) end.

//--- stdin.txt
A B