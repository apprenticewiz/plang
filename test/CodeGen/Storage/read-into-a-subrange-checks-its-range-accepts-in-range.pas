(*
RUN: split-file %s %t.dir
RUN: %plang -frange-checks %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:d=5
*)

(*
ISO Sec6.9.1 makes read(f, v) into `v := f^`, so Sec6.4.6's requirement that
the value lie within a subrange's interval applies exactly as it does to an
assignment written out. This is the in-range case: 5 is within 1..9 and is
accepted.
*)

//--- test.pas
program p(output);
var d: 1..9;
begin read(d); writeln('d=', d:1) end.

//--- stdin.txt
5
