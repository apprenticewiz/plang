(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ab
CHECK-NEXT:cd
*)

//--- test.pas
program p(input, output);
var c: char;
begin while not eoln do begin read(c); write(c) end; readln;
 writeln;
 while not eoln do begin read(c); write(c) end; writeln end.

//--- stdin.txt
ab
cd
