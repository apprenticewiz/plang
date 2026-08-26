(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:   0.333333333333333
*)

program p(output); var f: text; x: real;
begin rewrite(f, 'rt.txt'); writeln(f, 1.0/3.0); close(f);
 reset(f, 'rt.txt'); read(f, x); close(f);
 writeln(x:20:15) end.
