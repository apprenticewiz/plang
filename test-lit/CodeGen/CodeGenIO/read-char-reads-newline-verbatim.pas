(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:88 10
*)

//--- test.pas
program p;
var a, b: char;
begin read(a); read(b);
  writeln(ord(a), ' ', ord(b)) end.

//--- stdin.txt
X
Y