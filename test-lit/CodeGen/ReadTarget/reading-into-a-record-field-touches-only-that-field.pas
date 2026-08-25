(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5ZZZ
*)

//--- test.pas
program p(input, output);
type r = record a, b, c, d: char end;
var v: r;
begin
  v.a := 'Z'; v.b := 'Z'; v.c := 'Z'; v.d := 'Z';
  read(v.a);
  writeln(v.a, v.b, v.c, v.d)
end.

//--- stdin.txt
5
