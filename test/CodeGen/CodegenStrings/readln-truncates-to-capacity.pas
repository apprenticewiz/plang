(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcde] len=5
*)

//--- test.pas
program p;
var s: string(5);
begin readln(s); writeln('[', s, '] len=', length(s)) end.

//--- stdin.txt
abcdefghij
