(*
RUN: split-file %s %t.dir
RUN: %plang -fno-range-checks %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:d=99
*)

(*
Gated by the flag, like every other range check: -fno-range-checks is a
statement about what the program pays for, not about what is legal.
*)

//--- test.pas
program p(output);
var d: 1..9;
begin read(d); writeln('d=', d:1) end.

//--- stdin.txt
99
