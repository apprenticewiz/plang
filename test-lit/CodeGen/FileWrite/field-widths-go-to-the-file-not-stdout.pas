(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:   42  x    1.50 
*)

program p;
var f: text; c: char;
begin rewrite(f, 'plang_fw_test.txt');
 write(f, 42:5); write(f, 'x':3); writeln(f, 1.5:8:2);
 close(f); reset(f, 'plang_fw_test.txt');
 while not eof(f) do begin read(f, c); write(c) end;
 close(f) end.
