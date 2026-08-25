(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
CHECK-NEXT:42
*)

//--- test.pas
program p(input, output);
var c: char; i: integer;
begin read(c); readln; read(i); writeln(c); writeln(i) end.

//--- stdin.txt
5
42
