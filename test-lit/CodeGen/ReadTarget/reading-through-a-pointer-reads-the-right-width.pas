(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

//--- test.pas
program p(input, output);
var pc: ^char;
begin new(pc); pc^ := 'Z'; read(pc^); writeln(pc^) end.

//--- stdin.txt
5
