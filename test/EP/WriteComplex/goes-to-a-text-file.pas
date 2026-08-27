(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:( 1.5000000000000000e+000, 2.5000000000000000e+000) 
*)

program p(output); var f: text; z: complex; c: char;
begin z := cmplx(1.5, 2.5);
 rewrite(f, 'cplx.txt'); writeln(f, z); close(f);
 reset(f, 'cplx.txt');
 while not eof(f) do begin read(f, c); write(c) end;
 close(f) end.
