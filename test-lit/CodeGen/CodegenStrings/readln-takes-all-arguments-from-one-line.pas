(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a=1 b=2
*)

//--- test.pas
program p;
var a, b: integer;
begin readln(a, b); writeln('a=', a, ' b=', b) end.

//--- stdin.txt
1 2
