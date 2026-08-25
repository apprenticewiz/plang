(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:q
*)

//--- test.pas
program p(input, output);
var c: char;
begin if eof then writeln('empty') else begin read(c); writeln(c) end end.

//--- stdin.txt
q
