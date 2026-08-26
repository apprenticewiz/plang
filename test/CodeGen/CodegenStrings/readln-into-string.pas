(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello world]
*)

//--- test.pas
program p;
var s: string(20);
begin readln(s); writeln('[', s, ']') end.

//--- stdin.txt
hello world
