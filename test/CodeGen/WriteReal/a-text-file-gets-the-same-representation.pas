(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.00000000000000e+000 
*)

program p(output); var f: text; c: char;
begin rewrite(f, 'r.txt'); writeln(f, 1.0); close(f);
 reset(f, 'r.txt');
 while not eof(f) do begin read(f, c); write(c) end;
 close(f) end.
