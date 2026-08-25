(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:xy 7
*)

//--- test.pas
program p(input, output);
type letter = 'a'..'z';
var c1, c2: letter; n: integer;
begin read(c1, c2); readln(n); writeln(c1, c2, ' ', n) end.

//--- stdin.txt
xy
7
