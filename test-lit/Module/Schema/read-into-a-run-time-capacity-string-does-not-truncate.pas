(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello there world] len=17
*)

//--- test.pas
program p(input, output);
type ps = ^string;
var q: ps;
begin new(q, 25); readln(q^);
      writeln('[', q^, '] len=', length(q^):1) end.

//--- stdin.txt
hello there world
